

#include <stdlib.h>


#include "asss.h"



/* structs */

#include "packets/ppk.h"

#include "packets/kill.h"

#include "packets/shipchange.h"

#include "packets/green.h"

struct WeaponBuffer
{
	int dummy;
};


/* prototypes */

/* packet funcs */
local void Pppk(int, byte *, int);
local void PSpecRequest(int, byte *, int);
local void PSetShip(int, byte *, int);
local void PSetFreq(int, byte *, int);
local void PDie(int, byte *, int);
local void PGreen(int, byte *, int);
local void PAttach(int, byte *, int);


/* do weapons checksums */
local inline void DoChecksum(struct S2CWeapons *, int);

/* helper for SpecRequest, can be used by others */
local void SendPPK(int, int);

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

local PlayerData *players;
local ArenaData *arenas;

local Iassignfreq _myaf = { MyAssignFreq };

local struct C2SPosition pos[MAXPLAYERS];
/*local struct WeaponBuffer *wbuf; */

local int cfg_bulletpix, cfg_wpnpix, cfg_wpnbufsize, cfg_pospix;



int MM_game(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_ASSIGNFREQ, &afreq);
		mm->RegInterest(I_ARENAMAN, &aman);
		players = pd->players;

		if (!net || !cfg || !log || !aman) return MM_FAIL;
		
		arenas = aman->data;

		cfg_bulletpix = cfg->GetInt(GLOBAL, "Net", "BulletPixels", 1500);
		cfg_wpnpix = cfg->GetInt(GLOBAL, "Net", "WeaponPixels", 2000);
		cfg_wpnbufsize = cfg->GetInt(GLOBAL, "Net", "WeaponBuffer", 300);
		cfg_pospix = cfg->GetInt(GLOBAL, "Net", "PositionExtraPixels", 8192);

		net->AddPacket(C2S_POSITION, Pppk);
		net->AddPacket(C2S_SPECREQUEST, PSpecRequest);
		net->AddPacket(C2S_SETSHIP, PSetShip);
		net->AddPacket(C2S_SETFREQ, PSetFreq);
		net->AddPacket(C2S_DIE, PDie);
		net->AddPacket(C2S_GREEN, PGreen);
		net->AddPacket(C2S_ATTACHTO, PAttach);

		mm->RegInterface(I_ASSIGNFREQ, &_myaf);
	}
	else if (action == MM_UNLOAD)
	{
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
		/* do this last so we don't get prevented from unloading because
		 * of ourself */
		mm->UnregInterface(I_ASSIGNFREQ, &_myaf);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "game - handles position and weapons packets";
	}
	return MM_OK;
}


void Pppk(int pid, byte *p2, int n)
{
	/* LOCK: yeah, more stuff should really be locked here. but since
	 * this will be the most often-called handler by far, we can't
	 * afford it. */
	struct C2SPosition *p = (struct C2SPosition *)p2;
	int arena = players[pid].arena, sp = 0, i;
	int x1, y1, x2, y2, xr, yr, dist, cfg_pospix2 = cfg_pospix * cfg_pospix;
	static int set[MAXPLAYERS];

	/* handle common errors */
	if (arena < 0) return;
	if (players[pid].shiptype == SPEC) goto copypos;

	x1 = p->x;
	y1 = p->y;

	if (p->weapon.type)
	{
		struct S2CWeapons wpn = {
			S2C_WEAPON, p->rotation, (i16)p->time, p->x, p->yspeed,
			(u8)pid, p->flags, p->xspeed, 0, 0, p->y, p->bounty
		};
		* (i16*) &wpn.weapon = * (i16*) &p->weapon;

		for (i = 0; i < MAXPLAYERS; i++)
			if (	players[i].status == S_PLAYING &&
					players[i].arena == arena &&
					i != pid)
			{
				x2 = x1 - pos[i].x; y2 = y1 - pos[i].y;
				if (x2 < 0) x2 = -x2; if (y2 < 0) y2 = -y2;

				if (p->weapon.type < W_BOMB) /* HACK: bullet or bouncebullet */
				{
					if (x1 <= cfg_bulletpix || y1 <= cfg_bulletpix)
						set[sp++] = i;
				}
				else
				{
					if ( (x1 <= cfg_wpnpix && y1 <= cfg_wpnpix) ||
							p->weapon.type == W_THOR)
						set[sp++] = i;
				}
			}
		set[sp] = -1;
		DoChecksum(&wpn, sizeof(struct S2CWeapons));
		net->SendToSet(set, (byte*)&wpn, sizeof(struct S2CWeapons),
				NET_UNRELIABLE | NET_IMMEDIATE);
		/*BufferWeapon(); */
	}
	else
	{
		struct S2CPosition sendpos = { 
			S2C_POSITION, p->rotation, (i16)p->time, p->x, 0, (u8)pid,
			p->flags, p->yspeed, p->y, p->xspeed
		};

		for (i = 0; i < MAXPLAYERS; i++)
			if (	players[i].status == S_PLAYING &&
					players[i].arena == arena &&
					i != pid)
			{
				x2 = x1 - pos[i].x; y2 = y1 - pos[i].y;
				if (x2 < 0) x2 = -x2; if (y2 < 0) y2 = -y2;
				xr = players[i].xres; yr = players[i].yres;

				if (x2 <= xr && y2 <= yr)
					set[sp++] = i;
				else
				{
					dist = x2 - xr + y2 - yr;
					if (dist < cfg_pospix &&
							(rand() > (dist / cfg_pospix2 * dist * RAND_MAX)))
						set[sp++] = i;
				}
			}
		set[sp] = -1;
		net->SendToSet(set, (byte*)&sendpos, sizeof(struct S2CPosition),
				NET_UNRELIABLE | NET_IMMEDIATE);
		/*if (warped) SendOldWeapons(); */
	}

copypos:
	memcpy(pos + pid, p2, sizeof(pos[0]));
}


void PSpecRequest(int pid, byte *p, int n)
{
	int pid2 = ((struct SimplePacket*)p)->d1;
	SendPPK(pid, pid2);
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
		log->Log(LOG_BADDATA, "Bad ship number: %i (%s)", ship,
				players[pid].name);
		return;
	}

	if (arena < 0) return;

	to.freq = afreq->AssignFreq(arena, BADFREQ, ship);
	
	/* Arena.locked not defined yet */
	/*if (ship == SPEC && c->arenas[arena]->locked) return; */

	if (to.freq != BADFREQ)
	{
		players[pid].shiptype = ship;
		players[pid].freq = to.freq;
		net->SendToArena(arena, -1, (byte*)&to, 6, NET_RELIABLE);
	}
}


void PSetFreq(int pid, byte *p, int n)
{
	struct SimplePacket to = { S2C_FREQCHANGE, pid };
	int freq, arena, newfreq;

	freq = ((struct SimplePacket*)p)->d1;
	arena = players[pid].arena;

	if (arena < 0) return;

	newfreq = afreq->AssignFreq(arena, freq, players[pid].shiptype);

	if (newfreq != BADFREQ)
	{
		to.d2 = newfreq;
		players[pid].freq = newfreq;
		net->SendToArena(arena, -1, (byte*)&to, 5, NET_RELIABLE);
	}
}


void PDie(int pid, byte *p, int n)
{
	struct SimplePacket *dead = (struct SimplePacket*)p;
	struct KillPacket kp = { S2C_KILL };
	int killer = dead->d1;
	int bty = dead->d2;
	/* int flags = dead->d3; */
	int arena = players[pid].arena, reldeaths;

	if (arena < 0) return;

	kp.killer = killer;
	kp.bounty = bty;
	/* kp->flags = flags; */

	reldeaths = !!cfg->GetInt(arenas[arena].cfg,
			"Misc", "ReliableKills", 1);
	net->SendToArena(arena, pid, (byte*)&kp, sizeof(kp), NET_RELIABLE * reldeaths);

	/* handle points
	{
		Igivepoints *gp = mm->GetArenaInterface(arena, I_GIVEPOINTS);
		gp->Kill(arena, killer, pid, bty, flags);
	} */

	/* call callbacks */
	{
		Link *l;
		LinkedList *funcs = mm->LookupCallback(CALLBACK_KILL);
		for (l = LLGetHead(funcs); l; l = l->next)
			((KillFunc)l->data)(arena, killer, pid, bty, 0);
		LLFree(funcs);
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
		net->SendToArena(arena, -1, (byte*)&to, 5, NET_RELIABLE);
	pd->UnlockPlayer(pid2);
}


void DoChecksum(struct S2CWeapons *p, int n)
{
	int i;
	u8 ck = 0;
	for (i = 0; i < n; i++)
		ck ^= ((u8*)p)[i];
	p->checksum = ck;
}


void SendPPK(int pid, int to)
{
	struct C2SPosition *p = pos + pid;
	struct S2CPosition pos = { 
		S2C_POSITION, p->rotation, (i16)p->time, p->x, 0, (u8)pid,
		p->flags, p->yspeed, p->y, p->xspeed
	};
	net->SendToOne(pid, (byte*)&pos, sizeof(struct S2CPosition), NET_UNRELIABLE | NET_IMMEDIATE);
}



