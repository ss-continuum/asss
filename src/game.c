

#include <stdlib.h>

#include "asss.h"


#define WEAPONCOUNT 32

/* structs */

#include "packets/kill.h"
#include "packets/shipchange.h"
#include "packets/green.h"


/* prototypes */

/* packet funcs */
local void Pppk(int, byte *, int);
local void PSpecRequest(int, byte *, int);
local void PSetShip(int, byte *, int);
local void PSetFreq(int, byte *, int);
local void PDie(int, byte *, int);
local void PGreen(int, byte *, int);
local void PAttach(int, byte *, int);

local void Creport(const char *params, int pid, int target);

local inline void DoChecksum(struct S2CWeapons *, int);
local inline long lhypot (register long dx, register long dy);

/* helper for stuff */
local int MyAssignFreq(int, int, byte);



/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *log;
local Imodman *mm;
local Iassignfreq *afreq;
local Iarenaman *aman;
local Icmdman *cmd;
local Ichat *chat;

local PlayerData *players;
local ArenaData *arenas;

local Iassignfreq _myaf = { MyAssignFreq };

/* big arrays */
local struct C2SPosition pos[MAXPLAYERS];
local int speccing[MAXPLAYERS];

local int cfg_bulletpix, cfg_wpnpix, cfg_wpnbufsize, cfg_pospix;
local int wpnrange[WEAPONCOUNT]; /* there are 5 bits in the weapon type */


int MM_game(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_ASSIGNFREQ, &afreq);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_CHAT, &chat);

		if (!net || !cfg || !log || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		cfg_bulletpix = cfg->GetInt(GLOBAL, "Net", "BulletPixels", 1500);
		cfg_wpnpix = cfg->GetInt(GLOBAL, "Net", "WeaponPixels", 2000);
		cfg_wpnbufsize = cfg->GetInt(GLOBAL, "Net", "WeaponBuffer", 300);
		cfg_pospix = cfg->GetInt(GLOBAL, "Net", "PositionExtraPixels", 8192);

		for (i = 0; i < WEAPONCOUNT; i++)
			wpnrange[i] = cfg_wpnpix;
		/* exceptions: */
		wpnrange[W_BULLET] = cfg_bulletpix;
		wpnrange[W_BOUNCEBULLET] = cfg_bulletpix;
		wpnrange[W_THOR] = 30000;

		net->AddPacket(C2S_POSITION, Pppk);
		net->AddPacket(C2S_SPECREQUEST, PSpecRequest);
		net->AddPacket(C2S_SETSHIP, PSetShip);
		net->AddPacket(C2S_SETFREQ, PSetFreq);
		net->AddPacket(C2S_DIE, PDie);
		net->AddPacket(C2S_GREEN, PGreen);
		net->AddPacket(C2S_ATTACHTO, PAttach);

		cmd->AddCommand("report", Creport, 0);

		mm->RegInterface(I_ASSIGNFREQ, &_myaf);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("report", Creport);
		net->RemovePacket(C2S_POSITION, Pppk);
		net->RemovePacket(C2S_SETSHIP, PSetShip);
		net->RemovePacket(C2S_SETFREQ, PSetFreq);
		net->RemovePacket(C2S_DIE, PDie);
		net->RemovePacket(C2S_GREEN, PGreen);
		net->RemovePacket(C2S_ATTACHTO, PAttach);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_ASSIGNFREQ, &afreq);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_CHAT, &chat);
		/* do this last so we don't get prevented from unloading because
		 * of ourself */
		mm->UnregInterface(I_ASSIGNFREQ, &_myaf);
		return MM_OK;
	}
	return MM_FAIL;
}


void Pppk(int pid, byte *p2, int n)
{
	/* LOCK: yeah, more stuff should really be locked here. but since
	 * this will be the most often-called handler by far, we can't
	 * afford it. */
	struct PlayerPosition position;
	struct C2SPosition *p = (struct C2SPosition *)p2;
	int arena = players[pid].arena, sp = 0, i, sendwpn;
	int x1, y1;
	static int set[MAXPLAYERS];

	/* handle common errors */
	if (arena < 0) return;

	/* speccers don't get their position sent to anyone */
	if (players[pid].shiptype == SPEC)
	{
		if (speccing[pid] > -1)
			/* if he's speccing someone, set his position to be that
			 * player */
			memcpy(pos + pid, pos + speccing[pid], sizeof(pos[0]));
		else
			/* if not, he has his own position, so set it */
			memcpy(pos + pid, p2, sizeof(pos[0]));
		memset(&position, 0, sizeof(position));
		position.x = pos[pid].x;
		position.y = pos[pid].y;
		players[pid].position = position;
		return;
	}

	x1 = p->x;
	y1 = p->y;

	/* there are several reasons to send a weapon packet (05) instead of
	 * just a position one (28) */
	sendwpn = 0;
	/* if there's a real weapon */
	if (p->weapon.type > 0) sendwpn = 1;
	/* if the bounty is over 255 */
	if (p->bounty & 0xFF00) sendwpn = 1;
	/* if the pid is over 255 */
	if (pid & 0xFF00) sendwpn = 1;

	if (sendwpn)
	{
		int range = wpnrange[p->weapon.type];
		struct S2CWeapons wpn = {
			S2C_WEAPON, p->rotation, p->time & 0xFFFF, p->x, p->yspeed,
			pid, p->xspeed, 0, p->status, 0, p->y, p->bounty
		};
		wpn.weapon = p->weapon;

		for (i = 0; i < MAXPLAYERS; i++)
			if (	players[i].status == S_PLAYING
			     && players[i].arena == arena
			     && i != pid)
			{
				long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);
				if ( dist <= range ||
				     /* send mines to everyone too */
				     ( ( p->weapon.type == W_BOMB ||
				         p->weapon.type == W_PROXBOMB) &&
				       p->weapon.multimine) ||
				     /* and send some radar packets */
				     ( ( p->weapon.type == W_NULL &&
				         dist <= cfg_pospix &&
				         rand() > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0)))))
					set[sp++] = i;
			}
		set[sp] = -1;
		DoChecksum(&wpn, sizeof(struct S2CWeapons));
		net->SendToSet(set, (byte*)&wpn, sizeof(struct S2CWeapons),
				NET_UNRELIABLE | NET_IMMEDIATE);
	}
	else
	{
		struct S2CPosition sendpos = { 
			S2C_POSITION, p->rotation, p->time & 0xFFFF, p->x, 0,
			p->bounty, pid, p->status, p->yspeed, p->y, p->xspeed
		};

		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status == S_PLAYING &&
			    players[i].arena == arena &&
			    i != pid)
			{
				long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);
				int res = players[i].xres + players[i].yres;

				if (dist < res)
					set[sp++] = i;
				else if (
				    dist <= cfg_pospix
				 && (rand() > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0))))
						set[sp++] = i;
			}
		set[sp] = -1;
		net->SendToSet(set, (byte*)&sendpos, sizeof(struct S2CPosition),
				NET_UNRELIABLE | NET_IMMEDIATE);
	}

	memcpy(pos + pid, p2, sizeof(pos[0]));

	position.x = p2->x;
	position.y = p2->y;
	position.xspeed = p2->xspeed;
	position.yspeed = p2->yspeed;
	position.bounty = p2->bounty;
	position.status = p2->status;
	players[pid].position = position;
}


void PSpecRequest(int pid, byte *p, int n)
{
	int pid2 = ((struct SimplePacket*)p)->d1;
	speccing[pid] = pid2;
}


int MyAssignFreq(int pid, int requested, byte ship)
{
	int arena;
	ConfigHandle ch;

	pd->LockPlayer(pid);
	arena = players[pid].arena;
	pd->UnlockPlayer(pid);

	if (arena < 0) return BADFREQ;
	ch = arenas[arena].cfg;

	if (requested == BADFREQ)
	{
		if (ship == SPEC)
			return cfg->GetInt(ch, "Freq", "SpecFreq", 8025);
		else
			return 0; /* BalanceFreqs(arena); */
	}
	else
	{
		if (requested < 0 ||
				requested > cfg->GetInt(ch, "Freq", "MaxFreq", 9999))
			return BADFREQ;
		else if (ship == SPEC && cfg->GetInt(ch, "Freq", "LockSpec", 0))
			return BADFREQ;
		else
			return requested;
	}
}


void PSetShip(int pid, byte *p, int n)
{
	byte ship = p[1];
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, pid, 0 };
	int arena = players[pid].arena;

	if (/* ship < WARBIRD || */ ship > SPEC)
	{
		log->Log(L_MALICIOUS, "<game> {%s} [%s] Bad ship number: %d",
				arenas[arena].name,
				players[pid].name,
				ship);
		return;
	}

	if (arena < 0) return;

	to.freq = afreq->AssignFreq(arena, BADFREQ, ship);

	if (to.freq != BADFREQ)
	{
		players[pid].shiptype = ship;
		players[pid].freq = to.freq;
		net->SendToArena(arena, -1, (byte*)&to, 6, NET_RELIABLE);
		{
			LinkedList *lst = mm->LookupCallback(CALLBACK_SHIPCHANGE, arena);
			Link *l;
			for (l = LLGetHead(lst); l; l = l->next)
				((ShipChangeFunc)l->data)(pid, ship, freq);
		}
		log->Log(L_DRIVEL, "<game> {%s} [%s] Changed ship to %d",
				arenas[arena].name,
				players[pid].name,
				ship);
	}
}


void PSetFreq(int pid, byte *p, int n)
{
	struct SimplePacket to = { S2C_FREQCHANGE, pid, 0, -1};
	int freq, arena, newfreq;

	freq = ((struct SimplePacket*)p)->d1;
	arena = players[pid].arena;

	if (arena < 0) return;

	newfreq = afreq->AssignFreq(arena, freq, players[pid].shiptype);

	if (newfreq != BADFREQ)
	{
		to.d2 = newfreq;
		players[pid].freq = newfreq;
		net->SendToArena(arena, -1, (byte*)&to, 6, NET_RELIABLE);
		{
			LinkedList *lst = mm->LookupCallback(CALLBACK_FREQCHANGE, arena);
			Link *l;
			for (l = LLGetHead(lst); l; l = l->next)
				((FreqChangeFunc)l->data)(pid, newfreq);
		}
	}
}


void PDie(int pid, byte *p, int n)
{
	struct SimplePacket *dead = (struct SimplePacket*)p;
	struct KillPacket kp = { S2C_KILL };
	int killer = dead->d1;
	int bty = dead->d2;
	int flags = dead->d3;
	int arena = players[pid].arena, reldeaths;

	if (arena < 0) return;
	if (killer < 0 || killer >= MAXPLAYERS) return;
	if (players[killer].status != S_PLAYING) return;

	kp.unknown = 0;
	kp.killer = killer;
	kp.killed = pid;
	kp.bounty = bty;
	kp.flags = flags;

	reldeaths = !!cfg->GetInt(arenas[arena].cfg,
			"Misc", "ReliableKills", 1);
	net->SendToArena(arena, pid, (byte*)&kp, sizeof(kp), NET_RELIABLE * reldeaths);

	log->Log(L_DRIVEL, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d)",
			arenas[arena].name,
			players[pid].name,
			players[killer].name,
			bty,
			flags);

	/* call callbacks */
	{
		Link *l;
		LinkedList *funcs = mm->LookupCallback(CALLBACK_KILL, arena);
		for (l = LLGetHead(funcs); l; l = l->next)
			((KillFunc)l->data)(arena, killer, pid, bty, 0);
		mm->FreeLookupResult(funcs);
	}
}


void PGreen(int pid, byte *p, int n)
{
	struct GreenPacket *g = (struct GreenPacket *)p;
	int arena = players[pid].arena;
	g->pid = pid;
	g->type = S2C_GREEN; /* HACK :) */
	net->SendToArena(arena, pid, p, sizeof(struct GreenPacket), NET_UNRELIABLE);
	g->type = C2S_GREEN;
}


void PAttach(int pid, byte *p2, int n)
{
	struct SimplePacket *p = (struct SimplePacket*)p2;
	int pid2 = p->d1, arena;
	struct SimplePacket to = { S2C_TURRET, pid, pid2, 0, 0, 0 };

	arena = players[pid].arena;
	if (arena < 0) return;

	pd->LockPlayer(pid2);
	if (pid2 == -1 ||
			( players[pid].arena == players[pid2].arena &&
			  players[pid].freq  == players[pid2].freq) )
	{
		players[pid].attachedto = pid2;
		net->SendToArena(arena, -1, (byte*)&to, 5, NET_RELIABLE);
	}
	pd->UnlockPlayer(pid2);
}


void Creport(const char *params, int pid, int target)
{
	if (target < 0 || target >= MAXPLAYERS) return;

	if (chat)
	{
		struct C2SPosition *p = pos + target;
		chat->SendMessage(pid, "%s is at (%d, %d) with %d bounty and %d energy",
				players[target].name,
				p->x >> 4, p->y >> 4,
				p->bounty,
				p->energy);
	}
}


void DoChecksum(struct S2CWeapons *pkt, int n)
{
	int i;
	u8 ck = 0, *p = (u8*)pkt;
	for (i = 0; i < n; i++, p++)
		ck ^= *p;
	pkt->checksum = ck;
}


long lhypot (register long dx, register long dy)
{
	register unsigned long r, dd;

	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess
	 * (from Gems) */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));

	if (r == 0) return (long)r;

	/* converge
	 * 3 times
	 * */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

