
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"


#define WEAPONCOUNT 32

/* structs */

#include "packets/kill.h"
#include "packets/shipchange.h"
#include "packets/green.h"

#include "settings/game.h"

/* prototypes */
local void InitBrickSystem();
local void SendOldBricks(int pid);

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

local inline void DoChecksum(struct S2CWeapons *);
local inline long lhypot (register long dx, register long dy);

/* interface */
local void SetFreq(int pid, int freq);
local void SetShip(int pid, int ship);
local void SetFreqAndShip(int pid, int ship, int freq);
local void DropBrick(int arena, int freq, int x1, int y1, int x2, int y2, unsigned tm);
local void WarpTo(const Target *target, int x, int y);
local void GivePrize(const Target *target, int type, int count);

local Igame _myint =
{
	INTERFACE_HEAD_INIT(I_GAME, "game")
	SetFreq, SetShip, SetFreqAndShip, DropBrick, WarpTo, GivePrize
};


/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;
local Iflags *flags;
local Icapman *capman;
local Imapdata *mapdata;
local Ilagcollect *lagc;

local PlayerData *players;
local ArenaData *arenas;

/* big arrays */
local struct C2SPosition pos[MAXPLAYERS];
local int speccing[MAXPLAYERS];
local unsigned int wpnsent[MAXPLAYERS];
local struct { unsigned changes, lastcheck; } changes[MAXPLAYERS];
/* epd/energy stuff */
local struct { char see, cap, capnrg, pad__; } pl_epd[MAXPLAYERS];
local struct { char spec, nrg; } ar_epd[MAXARENA];

local int cfg_bulletpix, cfg_wpnpix, cfg_wpnbufsize, cfg_pospix;
local int cfg_sendanti, cfg_changelimit;
local int wpnrange[WEAPONCOUNT]; /* there are 5 bits in the weapon type */


EXPORT int MM_game(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flags = mm->GetInterface(I_FLAGS, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);

		if (!net || !cfg || !lm || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		cfg_bulletpix = cfg->GetInt(GLOBAL, "Net", "BulletPixels", 1500);
		cfg_wpnpix = cfg->GetInt(GLOBAL, "Net", "WeaponPixels", 2000);
		cfg_wpnbufsize = cfg->GetInt(GLOBAL, "Net", "WeaponBuffer", 300);
		cfg_pospix = cfg->GetInt(GLOBAL, "Net", "PositionExtraPixels", 8192);
		cfg_sendanti = cfg->GetInt(GLOBAL, "Net", "AntiwarpSendPercent", 5);
		/* convert to a percentage of RAND_MAX */
		cfg_sendanti = RAND_MAX / 100 * cfg_sendanti;
		cfg_changelimit = cfg->GetInt(GLOBAL, "General", "ShipChangeLimit", 10);

		for (i = 0; i < WEAPONCOUNT; i++)
			wpnrange[i] = cfg_wpnpix;
		for (i = 0; i < MAXPLAYERS; i++)
			speccing[i] = -1;

		/* exceptions: */
		wpnrange[W_BULLET] = cfg_bulletpix;
		wpnrange[W_BOUNCEBULLET] = cfg_bulletpix;
		wpnrange[W_THOR] = 30000;

		InitBrickSystem();

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		net->AddPacket(C2S_POSITION, Pppk);
		net->AddPacket(C2S_SPECREQUEST, PSpecRequest);
		net->AddPacket(C2S_SETSHIP, PSetShip);
		net->AddPacket(C2S_SETFREQ, PSetFreq);
		net->AddPacket(C2S_DIE, PDie);
		net->AddPacket(C2S_GREEN, PGreen);
		net->AddPacket(C2S_ATTACHTO, PAttach);
		net->AddPacket(C2S_TURRETKICKOFF, PKickoff);
		net->AddPacket(C2S_BRICK, PBrick);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
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
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(flags);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(lagc);
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
	int arena = players[pid].arena, i, sendwpn;
	struct pidset { int count, set[MAXPLAYERS+1]; } regset, epdset;
	int x1, y1;
	int sendtoall = 0, randnum = rand();

	/* handle common errors */
	if (ARENA_BAD(arena) || aman->arenas[arena].status != ARENA_RUNNING) return;

	/* do checksum */
	{
		byte checksum = 0;
		int left = 22;
		while (left--)
			checksum ^= p2[left];
		if (checksum != 0)
		{
			lm->Log(L_MALICIOUS, "<game> {%s} [%s] Bad position packet checksum",
					aman->arenas[arena].name,
					pd->players[pid].name);
			return;
		}
	}

	/* speccers don't get their position sent to anyone */
	if (players[pid].shiptype != SPEC)
	{
		/* epd thing */
		int see = SEE_NONE;
		/* because this might be SEE_TEAM */
		if (ar_epd[arena].nrg) see = ar_epd[arena].nrg;
		if (pl_epd[pid].capnrg) see = SEE_ALL;
		pl_epd[pid].see = see;

		x1 = p->x;
		y1 = p->y;

		regset.count = 0;
		epdset.count = 0;

		/* this is the weapons ignore hook */
		if (p->weapon.type && rand() < players[pid].ignoreweapons)
			p->weapon.type = 0;

		/* there are several reasons to send a weapon packet (05) instead of
		 * just a position one (28) */
		sendwpn = 0;
		/* if there's a real weapon */
		if (p->weapon.type > 0) sendwpn = 1;
		/* if the bounty is over 255 */
		if (p->bounty & 0xFF00) sendwpn = 1;
		/* if the pid is over 255 */
		if (pid & 0xFF00) sendwpn = 1;

		/* send mines to everyone */
		if ( ( p->weapon.type == W_BOMB ||
		       p->weapon.type == W_PROXBOMB) &&
		     p->weapon.alternate)
			sendtoall = 1;
		/* send some percent of antiwarp positions to everyone */
		if ( p->weapon.type == 0 &&
		     (p->status & 0x08) && /* FIXME: replace with symbolic constant or bitfield */
		     rand() < cfg_sendanti)
			sendtoall = 1;
		/* send safe zone enters to everyone */
		if ((p->status && 0x20) &&
		    !(players[pid].position.status & 0x20))
			sendtoall = 2;

		if (sendwpn)
		{
			int range = wpnrange[p->weapon.type], nflags;
			struct S2CWeapons wpn = {
				S2C_WEAPON, p->rotation, p->time & 0xFFFF, p->x, p->yspeed,
				pid, p->xspeed, 0, p->status, 0, p->y, p->bounty
			};
			wpn.weapon = p->weapon;
			wpn.extra = p->extra;

			nflags = NET_UNRELIABLE | (p->weapon.type ? NET_PRI_P5 : NET_PRI_P3);
			if (sendtoall == 2)
				nflags |= NET_RELIABLE;

			for (i = 0; i < MAXPLAYERS; i++)
				if (players[i].status == S_PLAYING &&
				    players[i].type != T_FAKE &&
				    players[i].arena == arena &&
				    i != pid)
				{
					struct pidset *set = &regset;
					long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);

					/* figure out epd thing */
					if (pl_epd[i].see == SEE_ALL ||
							( pl_epd[i].see == SEE_TEAM &&
							  players[pid].freq == players[i].freq) ||
							( pl_epd[i].see == SEE_SPEC &&
							  speccing[i] == pid ))
						set = &epdset;

					/* FIXME: the server needs to keep cont up to date
					 * on who's watching energy before it can send epd. */
					if (players[i].type == T_CONT)
						set = &regset;

					if (
							dist <= range ||
							sendtoall ||
							/* send it always to specers */
							speccing[i] == pid ||
							/* send it always to turreters */
							players[i].attachedto == pid ||
							/* and send some radar packets */
							( ( p->weapon.type == W_NULL &&
								dist <= cfg_pospix &&
								randnum > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0)))))
					{
						set->set[set->count++] = i;
						wpnsent[i]++;
					}
				}

			/* terminate sets */
			regset.set[regset.count] = -1;
			epdset.set[epdset.count] = -1;

			/* checksum and send packets */
			DoChecksum(&wpn);
			net->SendToSet(regset.set,
					(byte*)&wpn,
					sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData),
					nflags);
			net->SendToSet(epdset.set,
					(byte*)&wpn,
					sizeof(struct S2CWeapons),
					nflags);
		}
		else
		{
			int nflags;
			struct S2CPosition sendpos = {
				S2C_POSITION, p->rotation, p->time & 0xFFFF, p->x, 0,
				p->bounty, pid, p->status, p->yspeed, p->y, p->xspeed
			};

			nflags = NET_UNRELIABLE | NET_PRI_P3;
			if (sendtoall == 2)
				nflags |= NET_RELIABLE;

			for (i = 0; i < MAXPLAYERS; i++)
				if (players[i].status == S_PLAYING &&
				    players[i].type != T_FAKE &&
				    players[i].arena == arena &&
				    i != pid)
				{
					struct pidset *set = &regset;
					long dist = lhypot(x1 - pos[i].x, y1 - pos[i].y);
					int res = players[i].xres + players[i].yres;

					if (pl_epd[i].see == SEE_ALL ||
							( pl_epd[i].see == SEE_TEAM &&
							  players[pid].freq == players[i].freq) ||
							( pl_epd[i].see == SEE_SPEC &&
							  speccing[i] == pid ))
						set = &epdset;

					/* FIXME: see above */
					if (players[i].type == T_CONT)
						set = &regset;

					if (
							dist < res ||
							sendtoall ||
							/* send it always to specers */
							speccing[i] == pid ||
							/* send it always to turreters */
							players[i].attachedto == pid ||
							/* and send some radar packets */
							( dist <= cfg_pospix &&
							  (randnum > ((float)dist / (float)cfg_pospix * (RAND_MAX+1.0)))))
						set->set[set->count++] = i;
				}

			/* terminate sets */
			regset.set[regset.count] = -1;
			epdset.set[epdset.count] = -1;

			net->SendToSet(regset.set,
					(byte*)&sendpos,
					sizeof(struct S2CPosition) - sizeof(struct ExtraPosData),
					nflags);
			net->SendToSet(epdset.set,
					(byte*)&sendpos,
					sizeof(struct S2CPosition),
					nflags);
		}
	}
	else
	{
		int see = SEE_NONE;

		/* handle epd thing */
		if (ar_epd[arena].spec) see = ar_epd[arena].spec;
		if (pl_epd[pid].cap) see = SEE_SPEC;
		if (pl_epd[pid].capnrg) see = SEE_ALL;
		pl_epd[pid].see = see;
	}

	/* lag data */
	if (lagc)
		lagc->Position(pid, (GTC() - p->time) * 10, wpnsent[pid]);

	/* copy the whole thing. this will copy the epd, or, if the client
	 * didn't send any epd, it will copy zeros because the buffer was
	 * zeroed before data was recvd into it. */
	memcpy(pos + pid, p2, sizeof(pos[0]));

	/* update position in global players array */
	position.x = p->x;
	position.y = p->y;
	position.xspeed = p->xspeed;
	position.yspeed = p->yspeed;
	position.rotation = p->rotation;
	position.bounty = p->bounty;
	position.status = p->status;
	players[pid].position = position;
}


void PSpecRequest(int pid, byte *p, int n)
{
	int pid2 = ((struct SimplePacket*)p)->d1;
	if (players[pid].shiptype == SPEC)
		speccing[pid] = pid2;
}


local void reset_during_change(int pid, int success, void *dummy)
{
	pd->LockPlayer(pid);
	RESET_DURING_CHANGE(pid);
	pd->UnlockPlayer(pid);
}


void SetFreqAndShip(int pid, int ship, int freq)
{
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, pid, freq };
	int arena, set[] = { pid, -1 };

	if (PID_BAD(pid))
		return;
	arena = players[pid].arena;

	pd->LockPlayer(pid);

	if (players[pid].shiptype == ship &&
	    players[pid].freq == freq)
	{
		/* nothing to do */
		pd->UnlockPlayer(pid);
		return;
	}

	SET_DURING_CHANGE(pid);
	players[pid].shiptype = ship;
	speccing[pid] = -1;
	players[pid].freq = freq;

	pd->UnlockPlayer(pid);

	/* send it to him, with a callback */
	net->SendWithCallback(set, (byte*)&to, 6, reset_during_change, NULL);
	/* sent it to everyone else */
	net->SendToArena(arena, pid, (byte*)&to, 6, NET_RELIABLE);

	DO_CBS(CB_SHIPCHANGE, arena, ShipChangeFunc,
			(pid, ship, freq));

	lm->LogP(L_DRIVEL, "game", pid, "Changed ship/freq to ship %d, freq %d",
			ship, freq);
}

void SetShip(int pid, int ship)
{
	if (PID_OK(pid))
		SetFreqAndShip(pid, ship, players[pid].freq);
}

void PSetShip(int pid, byte *p, int n)
{
	int d;
	int arena = players[pid].arena;
	int ship = p[1], freq = players[pid].freq;
	Ifreqman *fm;

	if (ARENA_BAD(arena))
	{
		lm->Log(L_MALICIOUS, "<game> [%s] Ship request from bad arena",
				players[pid].name);
		return;
	}

	if (ship < WARBIRD || ship > SPEC)
	{
		lm->Log(L_MALICIOUS, "<game> {%s} [%s] Bad ship number: %d",
				arenas[arena].name,
				players[pid].name,
				ship);
		return;
	}

	if (IS_DURING_CHANGE(pid))
	{
		lm->Log(L_MALICIOUS, "<game> {%s} [%s] Ship request before ack from previous change",
				arenas[arena].name,
				players[pid].name);
		return;
	}

	/* exponential decay by 1/2 every 10 seconds */
	d = (GTC() - changes[pid].lastcheck) / 1000;
	changes[pid].changes >>= d;
	changes[pid].lastcheck += d * 1000;
	if (changes[pid].changes > cfg_changelimit && cfg_changelimit > 0)
	{
		Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm->LogP(L_INFO, "game", pid, "too many ship changes");
		/* disable for at least 30 seconds */
		changes[pid].changes |= (cfg_changelimit<<3);
		if (chat)
			chat->SendMessage(pid, "You're changing ships too often, disabling for 30 seconds.");
		mm->ReleaseInterface(chat);
		return;
	}
	changes[pid].changes++;

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		fm->ShipChange(pid, &ship, &freq);
		mm->ReleaseInterface(fm);
	}

	SetFreqAndShip(pid, ship, freq);
}


void SetFreq(int pid, int freq)
{
	struct SimplePacket to = { S2C_FREQCHANGE, pid, freq, -1};
	int arena = players[pid].arena, set[] = { pid, -1 };

	pd->LockPlayer(pid);

	if (players[pid].freq == freq)
	{
		pd->UnlockPlayer(pid);
		return;
	}

	SET_DURING_CHANGE(pid);
	players[pid].freq = freq;

	pd->UnlockPlayer(pid);

	/* him, with callback */
	net->SendWithCallback(set, (byte*)&to, 6, reset_during_change, NULL);
	/* everyone else */
	net->SendToArena(arena, pid, (byte*)&to, 6, NET_RELIABLE);

	DO_CBS(CB_FREQCHANGE, arena, FreqChangeFunc, (pid, freq));

	lm->Log(L_DRIVEL, "<game> {%s} [%s] Changed freq to %d",
			arenas[arena].name,
			players[pid].name,
			freq);
}

void PSetFreq(int pid, byte *p, int n)
{
	int freq, arena, ship;
	Ifreqman *fm;

	arena = players[pid].arena;
	freq = ((struct SimplePacket*)p)->d1;
	ship = players[pid].shiptype;

	if (ARENA_BAD(arena))
	{
		lm->Log(L_MALICIOUS, "<game> [%s] Freq change from bad arena",
				players[pid].name);
		return;
	}

	if (IS_DURING_CHANGE(pid))
	{
		lm->Log(L_MALICIOUS, "<game> {%s} [%s] Freq change before ack from previous change",
				arenas[arena].name,
				players[pid].name);
		return;
	}

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		fm->FreqChange(pid, &ship, &freq);
		mm->ReleaseInterface(fm);
	}

	if (ship == players[pid].shiptype)
		SetFreq(pid, freq);
	else
		SetFreqAndShip(pid, ship, freq);
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
	if (PID_BAD(killer)) return;
	if (players[killer].status != S_PLAYING) return;

	kp.green = 0;
	kp.killer = killer;
	kp.killed = pid;
	kp.bounty = bty;
	if (flags)
		flagcount = flags->GetCarriedFlags(pid);
	else
		flagcount = 0;
	kp.flags = flagcount;

	if (players[pid].freq == players[killer].freq)
		kp.bounty = 0;

	reldeaths = !!cfg->GetInt(arenas[arena].cfg,
			"Misc", "ReliableKills", 1);

	net->SendToArena(arena, -1, (byte*)&kp, sizeof(kp), NET_RELIABLE * reldeaths);

	lm->Log(L_DRIVEL, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d)",
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


void WarpTo(const Target *target, int x, int y)
{
	struct SimplePacket wto = { S2C_WARPTO, x, y };
	net->SendToTarget(target, (byte *)&wto, 5, NET_RELIABLE | NET_PRI_P1);
}


void GivePrize(const Target *target, int type, int count)
{
	struct SimplePacket prize = { S2C_PRIZERECV, (short)count, (short)type };
	net->SendToTarget(target, (byte*)&prize, 5, NET_RELIABLE);
}


void PlayerAction(int pid, int action, int arena)
{
	if (action == PA_ENTERARENA)
	{
		pl_epd[pid].see = 0;
		pl_epd[pid].cap = capman ? capman->HasCapability(pid, "seeepd") : 0;
		pl_epd[pid].capnrg = capman ? capman->HasCapability(pid, "seenrg") : 0;
		wpnsent[pid] = 0;
		SendOldBricks(pid);
	}
}


void ArenaAction(int arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		ar_epd[arena].spec =
			cfg->GetInt(arenas[arena].cfg, "Misc", "SpecSeeEnergy", 0);
		ar_epd[arena].nrg =
			cfg->GetInt(arenas[arena].cfg, "Misc", "SeeEnergy", 0);
	}
}


void DoChecksum(struct S2CWeapons *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData); i++)
		ck ^= ((unsigned char*)pkt)[i];
	pkt->checksum = ck;
}


long lhypot (register long dx, register long dy)
{
	register unsigned long r, dd;

	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess (from Gems) */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));

	if (r == 0) return (long)r;

	/* converge 3 times */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}


/* brick stuff below here */

#include "packets/brick.h"

static struct
{
	i16 cbrickid;
	u32 lasttime;
	LinkedList list;
	pthread_mutex_t mtx;
} brickdata[MAXARENA];


void InitBrickSystem()
{
	int i;
	for (i = 0; i < MAXARENA; i++)
	{
		brickdata[i].cbrickid = 0;
		LLInit(&brickdata[i].list);
		pthread_mutex_init(&brickdata[i].mtx, NULL);
	}
}


void DropBrick(int arena, int freq, int x1, int y1, int x2, int y2, unsigned tm)
{
	struct S2CBrickPacket *pkt = amalloc(sizeof(struct S2CBrickPacket));

	if (x2 < x1)
	{
		pkt->x1 = x2;
		pkt->x2 = x1;
	}
	else
	{
		pkt->x1 = x1;
		pkt->x2 = x2;
	}

	if (y2 < y1)
	{
		pkt->y1 = y2;
		pkt->y2 = y1;
	}
	else
	{
		pkt->y1 = y1;
		pkt->y2 = y2;
	}

	pkt->type = S2C_BRICK;
	pkt->freq = freq;

	pthread_mutex_lock(&brickdata[arena].mtx);
	pkt->brickid = brickdata[arena].cbrickid++;
	pkt->starttime = (tm == 0) ? GTC(): tm;
	/* stupid priitk */
	if (pkt->starttime <= brickdata[arena].lasttime)
		pkt->starttime = ++brickdata[arena].lasttime;
	else
		brickdata[arena].lasttime = pkt->starttime;
	LLAdd(&brickdata[arena].list, pkt);
	pthread_mutex_unlock(&brickdata[arena].mtx);

	net->SendToArena(arena, -1, (byte*)pkt, sizeof(*pkt), NET_RELIABLE | NET_PRI_P4);
	lm->Log(L_DRIVEL, "<game> {%s} Brick dropped (%d,%d)-(%d,%d) (freq=%d) (id=%d) (time=%u)",
			arenas[arena].name,
			x1, y1, x2, y2, freq,
			pkt->brickid, pkt->starttime);
}


void SendOldBricks(int pid)
{
	int arena = players[pid].arena, timeout, gtc;
	LinkedList *list = &brickdata[arena].list, toremove;
	Link *l;

	LLInit(&toremove);
	timeout = cfg->GetInt(arenas[arena].cfg, "Brick", "BrickTime", 0) + 50;
	
	pthread_mutex_lock(&brickdata[arena].mtx);
	gtc = GTC();

	for (l = LLGetHead(list); l; l = l->next)
	{
		struct S2CBrickPacket *pkt = (struct S2CBrickPacket*)l->data;
		if ( (pkt->starttime + timeout) > gtc )
			/* send it to the player */
			net->SendToOne(pid, (byte*)pkt, sizeof(*pkt), NET_RELIABLE);
		else
			/* mark it to be removed below */
			LLAdd(&toremove, pkt);
	}

	/* remove bricks marked above */
	for (l = LLGetHead(&toremove); l; l = l->next)
	{
		LLRemove(list, l->data);
		afree(l->data);
	}

	pthread_mutex_unlock(&brickdata[arena].mtx);

	LLEmpty(&toremove);
}


#if 0
void ClearAllBricks(int arena)
{
	byte pkt = S2C_BRICK;
	LinkedList *list = &brickdata[arena].list;
	Link *l;

	pthread_mutex_lock(&brickdata[arena].mtx);

	/* clear the list */
	for (l = LLGetHead(list); l; l = l->next)
		afree(l->data);
	LLEmpty(list);

	/* notify clients of all bricks gone */
	net->SendToArena(arena, -1, &pkt, 1, NET_RELIABLE);

	pthread_mutex_unlock(&brickdata[arena].mtx);
}
#endif


void PBrick(int pid, byte *p, int len)
{
	int dx, dy, x1, y1, x2, y2;
	int arena = players[pid].arena;
	int l;

	if (ARENA_BAD(arena)) return;

	l = cfg->GetInt(arenas[arena].cfg, "Brick", "BrickSpan", 10);

	dx = ((struct C2SBrickPacket*)p)->x;
	dy = ((struct C2SBrickPacket*)p)->y;

	mapdata->FindBrickEndpoints(arena, dx, dy, l, &x1, &y1, &x2, &y2);
	DropBrick(arena, players[pid].freq, x1, y1, x2, y2, 0);
}


