
#include <stdlib.h>

#include "asss.h"


#define WEAPONCOUNT 32

/* structs */

#include "packets/kill.h"
#include "packets/shipchange.h"
#include "packets/green.h"

#include "settings/game.h"

/* prototypes */

local void PlayerAction(int pid, int action, int arena);
local void ArenaAction(int arena, int action);

/* packet funcs */
local void Pppk(int, byte *, int);
local void PSpecRequest(int, byte *, int);
local void PSetShip(int, byte *, int);
local void PSetFreq(int, byte *, int);
local void PDie(int, byte *, int);
local void PGreen(int, byte *, int);
local void PAttach(int, byte *, int);
local void PKickoff(int, byte *, int);
local void PBrick(int, byte *, int);

local void Creport(const char *params, int pid, int target);

local inline void DoChecksum(struct S2CWeapons *, int);
local inline long lhypot (register long dx, register long dy);

/* interface? */
local void SetFreq(int pid, int freq);
local void SetShip(int pid, int ship, int freq);
local void DropBrick(int arena, int freq, int x1, int y1, int x2, int y2);

local Igame _myint = { SetFreq, SetShip, DropBrick };


/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *log;
local Imodman *mm;
local Iarenaman *aman;
local Icmdman *cmd;
local Ichat *chat;
local Iflags *flags;
local Icapman *capman;
local Imapdata *mapdata;

local PlayerData *players;
local ArenaData *arenas;

/* big arrays */
local struct C2SPosition pos[MAXPLAYERS];
local int speccing[MAXPLAYERS];
/* epd/energy stuff */
local struct { char see, cap, capnrg, pad__; } pl_epd[MAXPLAYERS];
local struct { char spec, nrg; } ar_epd[MAXARENA];

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
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_CHAT, &chat);
		mm->RegInterest(I_FLAGS, &flags);
		mm->RegInterest(I_CAPMAN, &capman);
		mm->RegInterest(I_MAPDATA, &mapdata);

		if (!net || !cfg || !log || !aman) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

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
		net->AddPacket(C2S_TURRETKICKOFF, PKickoff);
		net->AddPacket(C2S_BRICK, PBrick);

		cmd->AddCommand("report", Creport);

		mm->RegInterface(I_GAME, &_myint);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_GAME, &_myint);
		cmd->RemoveCommand("report", Creport);
		net->RemovePacket(C2S_POSITION, Pppk);
		net->RemovePacket(C2S_SETSHIP, PSetShip);
		net->RemovePacket(C2S_SETFREQ, PSetFreq);
		net->RemovePacket(C2S_DIE, PDie);
		net->RemovePacket(C2S_GREEN, PGreen);
		net->RemovePacket(C2S_ATTACHTO, PAttach);
		net->RemovePacket(C2S_TURRETKICKOFF, PKickoff);
		net->RemovePacket(C2S_BRICK, PBrick);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_FLAGS, &flags);
		mm->UnregInterest(I_CAPMAN, &capman);
		mm->UnregInterest(I_MAPDATA, &mapdata);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void Pppk(int pid, byte *p2, int n)
{
	/* LOCK: yeah, more stuff should really be locked here. but since
	 * this will be the most often-called handler by far, we can't
	 * afford it. */
	struct PlayerPosition position;
	struct C2SPosition *p = (struct C2SPosition *)p2;
	int arena = players[pid].arena, i, sendwpn;
	int x1, y1;
	int regset[MAXPLAYERS+1], epdset[MAXPLAYERS+1];

	/* handle common errors */
	if (arena < 0) return;

	/* speccers don't get their position sent to anyone */
	if (players[pid].shiptype == SPEC)
	{
		int see = SEE_NONE;

		if (speccing[pid] >= 0)
			/* if he's speccing someone, set his position to be that player */
			memcpy(pos + pid, pos + speccing[pid], sizeof(pos[0]));
		else
			/* if not, he has his own position, so set it */
			memcpy(pos + pid, p2, sizeof(pos[0]));
		memset(&position, 0, sizeof(position));
		position.x = pos[pid].x;
		position.y = pos[pid].y;
		players[pid].position = position;

		/* handle epd thing */
		if (ar_epd[arena].spec) see = ar_epd[arena].spec;
		if (pl_epd[pid].cap) see = SEE_SPEC;
		if (pl_epd[pid].capnrg) see = SEE_ALL;
		pl_epd[pid].see = see;

		/* and don't send out packets */
		return;
	}
	else
	{
		/* epd thing */
		int see = SEE_NONE;
		/* because this might be SEE_TEAM */
		if (ar_epd[arena].nrg) see = ar_epd[arena].nrg;
		if (pl_epd[pid].capnrg) see = SEE_ALL;
		pl_epd[pid].see = see;
	}

	x1 = p->x;
	y1 = p->y;

	regset[0] = 1;
	epdset[0] = 1;

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
		int nflags = NET_UNRELIABLE;
		struct S2CWeapons wpn = {
			S2C_WEAPON, p->rotation, p->time & 0xFFFF, p->x, p->yspeed,
			pid, p->xspeed, 0, p->status, 0, p->y, p->bounty
		};
		wpn.weapon = p->weapon;
		wpn.extra = p->extra;

		if (p->weapon.type) nflags |= NET_IMMEDIATE;

		for (i = 0; i < MAXPLAYERS; i++)
			if (	players[i].status == S_PLAYING
			     && players[i].arena == arena
			     && i != pid)
			{
				int *set = regset;
				long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);

				if (pl_epd[i].see == SEE_ALL ||
				    ( pl_epd[i].see == SEE_TEAM &&
				      players[pid].freq == players[i].freq) ||
				    ( pl_epd[i].see == SEE_SPEC &&
				      speccing[i] == pid ))
					set = epdset;

				/* figure out epd thing */
				if ( dist <= range ||
				     /* send mines to everyone too */
				     ( ( p->weapon.type == W_BOMB ||
				         p->weapon.type == W_PROXBOMB) &&
				       p->weapon.multimine) ||
				     /* and send some radar packets */
				     ( ( p->weapon.type == W_NULL &&
				         dist <= cfg_pospix &&
				         rand() > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0)))))
					set[set[0]++] = i;
			}
		/* send regular */
		DoChecksum(&wpn, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData));
		regset[regset[0]] = -1;
		net->SendToSet(regset + 1,
		               (byte*)&wpn,
		               sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData),
		               nflags);
		/* send epd */
		epdset[epdset[0]] = -1;
		net->SendToSet(epdset + 1,
		               (byte*)&wpn,
		               sizeof(struct S2CWeapons),
		               nflags);
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
				int *set = regset;
				long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);
				int res = players[i].xres + players[i].yres;

				if (pl_epd[i].see == SEE_ALL ||
				    ( pl_epd[i].see == SEE_TEAM &&
				      players[pid].freq == players[i].freq) ||
				    ( pl_epd[i].see == SEE_SPEC &&
				      speccing[i] == pid ))
					set = epdset;

				if (dist < res)
					set[set[0]++] = i;
				else if (
				    dist <= cfg_pospix
				 && (rand() > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0))))
						set[set[0]++] = i;
			}
		regset[regset[0]] = -1;
		epdset[epdset[0]] = -1;
		net->SendToSet(regset + 1,
		               (byte*)&sendpos,
		               sizeof(struct S2CPosition) - sizeof(struct ExtraPosData),
		               NET_UNRELIABLE);
		net->SendToSet(epdset + 1,
		               (byte*)&sendpos,
		               sizeof(struct S2CPosition),
		               NET_UNRELIABLE);
	}

	/* copy the whole thing. this will copy the epd, or, if the client
	 * didn't send any epd, it will copy zeros because the buffer was
	 * zeroed before data was recvd into it. */
	memcpy(pos + pid, p2, sizeof(pos[0]));

	position.x = p->x;
	position.y = p->y;
	position.xspeed = p->xspeed;
	position.yspeed = p->yspeed;
	position.bounty = p->bounty;
	position.status = p->status;
	players[pid].position = position;
}


void PSpecRequest(int pid, byte *p, int n)
{
	int pid2 = ((struct SimplePacket*)p)->d1;
	speccing[pid] = pid2;
}


void SetShip(int pid, int ship, int freq)
{
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, pid, freq };
	int arena = players[pid].arena;

	pd->LockPlayer(pid);
	players[pid].shiptype = ship;
	players[pid].freq = freq;
	net->SendToArena(arena, -1, (byte*)&to, 6, NET_RELIABLE);
	pd->UnlockPlayer(pid);

	DO_CBS(CB_SHIPCHANGE, arena, ShipChangeFunc,
			(pid, ship, freq));

	log->Log(L_DRIVEL, "<game> {%s} [%s] Changed ship to %d",
			arenas[arena].name,
			players[pid].name,
			ship);
}

void PSetShip(int pid, byte *p, int n)
{
	int ship = p[1];
	int arena = players[pid].arena;
	int freq = players[pid].freq;

	if (ship < WARBIRD || ship > SPEC)
	{
		log->Log(L_MALICIOUS, "<game> {%s} [%s] Bad ship number: %d",
				arenas[arena].name,
				players[pid].name,
				ship);
		return;
	}

	if (arena < 0 || arena >= MAXARENA)
	{
		log->Log(L_MALICIOUS, "<game> [%s] Ship request from bad arena",
				players[pid].name);
		return;
	}

	DO_CBS(CB_FREQMANAGER,
	       arena,
	       FreqManager,
	       (pid, REQUEST_SHIP, &ship, &freq));

	SetShip(pid, ship, freq);
}


void SetFreq(int pid, int freq)
{
	struct SimplePacket to = { S2C_FREQCHANGE, pid, freq, -1};
	int arena = players[pid].arena;

	pd->LockPlayer(pid);
	players[pid].freq = freq;
	net->SendToArena(arena, -1, (byte*)&to, 6, NET_RELIABLE);
	pd->UnlockPlayer(pid);

	DO_CBS(CB_FREQCHANGE, arena, FreqChangeFunc, (pid, freq));

	log->Log(L_DRIVEL, "<game> {%s} [%s] Changed freq to %d",
			arenas[arena].name,
			players[pid].name,
			freq);
}

void PSetFreq(int pid, byte *p, int n)
{
	int freq, arena, ship;

	arena = players[pid].arena;
	freq = ((struct SimplePacket*)p)->d1;
	ship = players[pid].shiptype;

	if (arena < 0 || arena >= MAXARENA) return;

	DO_CBS(CB_FREQMANAGER,
	       arena,
	       FreqManager,
	       (pid, REQUEST_SHIP, &ship, &freq));

	if (ship == players[pid].shiptype)
		SetFreq(pid, freq);
	else
		SetShip(pid, ship, freq);
}


void PDie(int pid, byte *p, int n)
{
	struct SimplePacket *dead = (struct SimplePacket*)p;
	struct KillPacket kp = { S2C_KILL };
	int killer = dead->d1;
	int bty = dead->d2;
	int flagcount;
	int arena = players[pid].arena, reldeaths;

	if (arena < 0) return;
	if (killer < 0 || killer >= MAXPLAYERS) return;
	if (players[killer].status != S_PLAYING) return;

	kp.unknown = 0;
	kp.killer = killer;
	kp.killed = pid;
	kp.bounty = bty;
	if (flags)
		flagcount = flags->GetCarriedFlags(pid);
	else
		flagcount = 0;
	kp.flags = flagcount;

	reldeaths = !!cfg->GetInt(arenas[arena].cfg,
			"Misc", "ReliableKills", 1);
	net->SendToArena(arena, -1, (byte*)&kp, sizeof(kp), NET_RELIABLE * reldeaths);

	log->Log(L_DRIVEL, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d)",
			arenas[arena].name,
			players[pid].name,
			players[killer].name,
			bty,
			flagcount);

	/* call callbacks */
	DO_CBS(CB_KILL, arena, KillFunc,
			(arena, killer, pid, bty, flagcount));
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


void PKickoff(int pid, byte *p, int len)
{
	int i;
	byte pkt = S2C_TURRETKICKOFF;
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING &&
		    players[i].attachedto == pid)
			net->SendToOne(i, &pkt, 1, NET_RELIABLE);
}


#include "packets/brick.h"

void DropBrick(int arena, int freq, int x1, int y1, int x2, int y2)
{
	/* static data here */
	static i16 brickids[MAXARENA] = { 0 };
	static pthread_mutex_t brickmtx = PTHREAD_MUTEX_INITIALIZER;
	/* end static data */

	struct S2CBrickPacket pkt =
		{ S2C_BRICK, x1, y1, x2, y2, freq };

	/* re-order points if necessary */
	if (pkt.x2 < pkt.x1)
	{
		i16 t = pkt.x2;
		pkt.x2 = pkt.x1;
		pkt.x1 = t;
	}
	if (pkt.y2 < pkt.y1)
	{
		i16 t = pkt.y2;
		pkt.y2 = pkt.y1;
		pkt.y1 = t;
	}

	pthread_mutex_lock(&brickmtx);
	pkt.brickid = brickids[arena]++;
	pthread_mutex_unlock(&brickmtx);
	pkt.starttime = GTC();
	net->SendToArena(arena, -1, (byte*)&pkt, sizeof(pkt), NET_RELIABLE | NET_IMMEDIATE);
	log->Log(L_DRIVEL, "<game> {%s} Brick dropped (%d,%d)-(%d,%d) (freq=%d)",
			arenas[arena].name,
			x1, y1, x2, y2, freq);
}


void PBrick(int pid, byte *p, int len)
{
	int dx, dy, x1, y1, x2, y2;
	int arena = players[pid].arena;
	int l;

	if (arena < 0 || arena >= MAXARENA) return;

	l = cfg->GetInt(arenas[arena].cfg, "Brick", "BrickSpan", 10);

	dx = ((struct C2SBrickPacket*)p)->x;
	dy = ((struct C2SBrickPacket*)p)->y;

	mapdata->FindBrickEndpoints(arena, dx, dy, l, &x1, &y1, &x2, &y2);
	DropBrick(arena, players[pid].freq, x1, y1, x2, y2);
}


void PlayerAction(int pid, int action, int arena)
{
	if (action == PA_ENTERARENA)
	{
		pl_epd[pid].see = 0;
		pl_epd[pid].cap = capman ? capman->HasCapability(pid, "seeepd") : 0;
		pl_epd[pid].capnrg = capman ? capman->HasCapability(pid, "seenrg") : 0; 
	}
}

void ArenaAction(int arena, int action)
{
	if (action == AA_CREATE)
	{
		ar_epd[arena].spec =
			cfg->GetInt(arenas[arena].cfg, "Misc", "SpecSeeEnergy", 0);
		ar_epd[arena].nrg =
			cfg->GetInt(arenas[arena].cfg, "Misc", "SeeEnergy", 0);
	}
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
	pkt->checksum = 0;
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

