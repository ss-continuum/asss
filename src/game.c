
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "clientset.h"


#define WEAPONCOUNT 32

/* structs */

#include "packets/kill.h"
#include "packets/shipchange.h"
#include "packets/green.h"
#include "packets/brick.h"

#include "settings/game.h"


/* these are bit positions for the personalgreen field */
enum { personal_thor, personal_burst, personal_brick };

typedef struct
{
	struct C2SPosition pos;
	Player *speccing;
	unsigned int wpnsent;
	struct { int changes; unsigned lastcheck; } changes;
	/* epd/energy stuff */
	struct { int seenrg, seenrgspec, seeepd; } pl_epd;
	/*           enum    enum        bool              */
	int lockship;
} pdata;

typedef struct
{
	int spec_epd, spec_nrg, all_nrg;
	/*  bool      enum      enum     */
	unsigned long personalgreen;
	int initlockship, initspec;
} adata;

typedef struct
{
	u16 cbrickid;
	u16 countbricksaswalls;
	ticks_t lasttime;
	LinkedList list;
	pthread_mutex_t mtx;
} brickdata;


/* prototypes */
local void SendOldBricks(Player *p);

local void PlayerAction(Player *p, int action, Arena *arena);
local void ArenaAction(Arena *arena, int action);

/* packet funcs */
local void Pppk(Player *, byte *, int);
local void PSpecRequest(Player *, byte *, int);
local void PSetShip(Player *, byte *, int);
local void PSetFreq(Player *, byte *, int);
local void PDie(Player *, byte *, int);
local void PGreen(Player *, byte *, int);
local void PAttach(Player *, byte *, int);
local void PKickoff(Player *, byte *, int);
local void PBrick(Player *, byte *, int);

local void MChangeFreq(Player *p, const char *line);

local inline void DoChecksum(struct S2CWeapons *);
local inline long lhypot (register long dx, register long dy);

/* interface */
local void SetFreq(Player *p, int freq);
local void SetShip(Player *p, int ship);
local void SetFreqAndShip(Player *p, int ship, int freq);
local void DropBrick(Arena *arena, int freq, int x1, int y1, int x2, int y2);
local void WarpTo(const Target *target, int x, int y);
local void GivePrize(const Target *target, int type, int count);
local void Lock(const Target *t, int notify, int spec);
local void Unlock(const Target *t, int notify);
local void LockArena(Arena *a, int notify, int onlyarenastate, int initial, int spec);
local void UnlockArena(Arena *a, int notify, int onlystate);
local void FakePosition(Player *p, struct C2SPosition *pos, int len);
local void FakeKill(Player *killer, Player *killed, int bounty, int flags);

local Igame _myint =
{
	INTERFACE_HEAD_INIT(I_GAME, "game")
	SetFreq, SetShip, SetFreqAndShip, DropBrick, WarpTo, GivePrize,
	Lock, Unlock, LockArena, UnlockArena,
	FakePosition, FakeKill
};


/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;
local Iflags *flags;
local Icapman *capman;
local Imapdata *mapdata;
local Ilagcollect *lagc;
local Ichat *chat;

/* big arrays */
local int adkey, brickkey, pdkey;

local int cfg_bulletpix, cfg_wpnpix, cfg_pospix;
local int cfg_sendanti, cfg_changelimit;
local int wpnrange[WEAPONCOUNT]; /* there are 5 bits in the weapon type */


EXPORT int MM_game(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flags = mm->GetInterface(I_FLAGS, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!net || !cfg || !lm || !aman) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		brickkey = aman->AllocateArenaData(sizeof(brickdata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (adkey == -1 || brickkey == -1 || pdkey == -1) return MM_FAIL;

		/* cfghelp: Net:BulletPixels, global, int, def: 1500
		 * How far away to always send bullets (in pixels). */
		cfg_bulletpix = cfg->GetInt(GLOBAL, "Net", "BulletPixels", 1500);
		/* cfghelp: Net:WeaponPixels, global, int, def: 2000
		 * How far away to always send weapons (in pixels). */
		cfg_wpnpix = cfg->GetInt(GLOBAL, "Net", "WeaponPixels", 2000);
		/* cfghelp: Net:PositionExtraPixels, global, int, def: 8000
		 * How far away to send positions of players on radar. */
		cfg_pospix = cfg->GetInt(GLOBAL, "Net", "PositionExtraPixels", 8000);
		/* cfghelp: Net:AntiwarpSendPercent, global, int, def: 5
		 * Percent of position packets with antiwarp enabled to send to
		 * the whole arena. */
		cfg_sendanti = cfg->GetInt(GLOBAL, "Net", "AntiwarpSendPercent", 5);
		/* convert to a percentage of RAND_MAX */
		cfg_sendanti = RAND_MAX / 100 * cfg_sendanti;
		/* cfghelp: General:ShipChangeLimit, global, int, def: 10
		 * The number of ship changes in a short time (about 10 seconds)
		 * before ship changing is disabled (for about 30 seconds). */
		cfg_changelimit = cfg->GetInt(GLOBAL, "General", "ShipChangeLimit", 10);

		for (i = 0; i < WEAPONCOUNT; i++)
			wpnrange[i] = cfg_wpnpix;
		/* exceptions: */
		wpnrange[W_BULLET] = cfg_bulletpix;
		wpnrange[W_BOUNCEBULLET] = cfg_bulletpix;
		wpnrange[W_THOR] = 30000;

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

		if (chatnet)
			chatnet->AddHandler("CHANGEFREQ", MChangeFreq);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		if (chatnet)
			chatnet->RemoveHandler("CHANGEFREQ", MChangeFreq);
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
		aman->FreeArenaData(adkey);
		aman->FreeArenaData(brickkey);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(flags);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(lagc);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	return MM_FAIL;
}


void Pppk(Player *p, byte *p2, int n)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	pdata *data = PPDATA(p, pdkey), *idata;
	int sendwpn, x1, y1;
	int sendtoall = 0, randnum = rand();
	Player *i;
	LinkedList
		regset = LL_INITIALIZER,
		nrgset = LL_INITIALIZER,
		epdset = LL_INITIALIZER;
	Link *link;
	ticks_t gtc = current_ticks();
	int latency = TICK_DIFF(gtc, pos->time);

	if (latency < 0) latency = 0;
	if (latency > 255) latency = 255;

	/* handle common errors */
	if (!arena || arena->status != ARENA_RUNNING || p->status != S_PLAYING) return;

	/* do checksum */
	if (p->type != T_FAKE)
	{
		byte checksum = 0;
		int left = 22;
		while (left--)
			checksum ^= p2[left];
		if (checksum != 0)
		{
			lm->LogP(L_MALICIOUS, "game", p, "bad position packet checksum");
			return;
		}
	}

	/* speccers don't get their position sent to anyone */
	if (p->p_ship != SPEC)
	{
		x1 = pos->x;
		y1 = pos->y;

		/* this is the weapons ignore hook */
		if (pos->weapon.type && (unsigned)rand() < p->ignoreweapons)
			pos->weapon.type = 0;

		/* there are several reasons to send a weapon packet (05) instead of
		 * just a position one (28) */
		sendwpn = 0;
		/* if there's a real weapon */
		if (pos->weapon.type > 0) sendwpn = 1;
		/* if the bounty is over 255 */
		if (pos->bounty & 0xFF00) sendwpn = 1;
		/* if the pid is over 255 */
		if (p->pid & 0xFF00) sendwpn = 1;

		/* send mines to everyone */
		if ( ( pos->weapon.type == W_BOMB ||
		       pos->weapon.type == W_PROXBOMB) &&
		     pos->weapon.alternate)
			sendtoall = 1;
		/* send some percent of antiwarp positions to everyone */
		if ( pos->weapon.type == 0 &&
		     (pos->status & STATUS_ANTIWARP) &&
		     rand() < cfg_sendanti)
			sendtoall = 1;
		/* send safe zone enters to everyone, reliably */
		if ((pos->status & STATUS_SAFEZONE) &&
		    !(p->position.status & STATUS_SAFEZONE))
			sendtoall = 2;

		if (sendwpn)
		{
			int range = wpnrange[pos->weapon.type], nflags;
			struct S2CWeapons wpn = {
				S2C_WEAPON, pos->rotation, gtc & 0xFFFF, pos->x, pos->yspeed,
				p->pid, pos->xspeed, 0, pos->status, (u8)latency,
				pos->y, pos->bounty
			};
			wpn.weapon = pos->weapon;
			wpn.extra = pos->extra;

			if (sendtoall != 2)
				nflags = NET_UNRELIABLE;
			else
				nflags = NET_RELIABLE;

			if (wpn.weapon.type == 0)
				nflags |= NET_PRI_P3 | NET_DROPPABLE;
			else
				nflags |= NET_PRI_P5;

			pd->Lock();
			FOR_EACH_PLAYER_P(i, idata, pdkey)
				if (i->status == S_PLAYING &&
				    IS_STANDARD(i) &&
				    i->arena == arena &&
				    i != p)
				{
					long dist = lhypot(x1 - idata->pos.x, y1 - idata->pos.y);

					if (
							dist <= range ||
							sendtoall ||
							/* send it always to specers */
							data->speccing == p ||
							/* send it always to turreters */
							i->p_attached == p->pid ||
							/* and send some radar packets */
							( ( wpn.weapon.type == W_NULL &&
							    dist <= cfg_pospix &&
							    randnum > ((float)dist / (float)cfg_pospix *
							        (RAND_MAX+1.0)))) ||
							/* bots */
							i->flags.see_all_posn)
					{
						/* figure out which set to add him to */
						LinkedList *set = &regset;
						if (pos->extra.energy == 0)
						{
							/* use this to signal the epd doesn't exist; don't
							 * send. */
						}
						else if (i->p_ship == SPEC)
						{
							if (idata->pl_epd.seenrgspec == SEE_ALL ||
							    ( idata->pl_epd.seenrgspec == SEE_TEAM &&
							      p->p_freq == i->p_freq ) ||
							    ( idata->pl_epd.seenrgspec == SEE_SPEC &&
							      idata->speccing == p ))
								set = &nrgset;
							/* for spectators, also check epd */
							if (idata->pl_epd.seeepd)
								set = &epdset;
						}
						else
						{
							if (idata->pl_epd.seenrg == SEE_ALL ||
							    ( idata->pl_epd.seenrg == SEE_TEAM &&
							      p->p_freq == i->p_freq ) ||
							    ( idata->pl_epd.seenrg == SEE_SPEC &&
							      idata->speccing == p ))
								set = &nrgset;
						}

						LLAdd(set, i);
						if (wpn.weapon.type)
							idata->wpnsent++;
					}
				}
			pd->Unlock();

			/* checksum and send packets */
			DoChecksum(&wpn);
			net->SendToSet(&regset,
					(byte*)&wpn,
					sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData),
					nflags);
			net->SendToSet(&nrgset,
					(byte*)&wpn,
					sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData) + 2,
					nflags);
			net->SendToSet(&epdset,
					(byte*)&wpn,
					sizeof(struct S2CWeapons),
					nflags);
		}
		else
		{
			int nflags;
			struct S2CPosition sendpos = {
				S2C_POSITION, pos->rotation, gtc & 0xFFFF, pos->x, (u8)latency,
				pos->bounty, (u8)p->pid, pos->status, pos->yspeed, pos->y, pos->xspeed
			};
			sendpos.extra = pos->extra;

			nflags = NET_UNRELIABLE | NET_PRI_P3 | NET_DROPPABLE;
			if (sendtoall == 2)
				nflags |= NET_RELIABLE;

			pd->Lock();
			FOR_EACH_PLAYER_P(i, idata, pdkey)
				if (i->status == S_PLAYING &&
				    IS_STANDARD(i) &&
				    i->arena == arena &&
				    i != p)
				{
					long dist = lhypot(x1 - idata->pos.x, y1 - data->pos.y);
					int res = i->xres + i->yres;

					if (
							dist < res ||
							sendtoall ||
							/* send it always to specers */
							idata->speccing == p ||
							/* send it always to turreters */
							i->p_attached == p->pid ||
							/* and send some radar packets */
							( dist <= cfg_pospix &&
							  (randnum > ((float)dist / (float)cfg_pospix *
							              (RAND_MAX+1.0)))) ||
							/* bots */
							i->flags.see_all_posn)
					{
						/* figure out which set to add him to */
						LinkedList *set = &regset;
						if (pos->extra.energy == 0)
						{
							/* use this to signal the epd doesn't exist; don't
							 * send. */
						}
						else if (i->p_ship == SPEC)
						{
							if (idata->pl_epd.seenrgspec == SEE_ALL ||
							    ( idata->pl_epd.seenrgspec == SEE_TEAM &&
							      p->p_freq == i->p_freq ) ||
							    ( idata->pl_epd.seenrgspec == SEE_SPEC &&
							      idata->speccing == p ))
								set = &nrgset;
							/* for spectators, also check epd */
							if (idata->pl_epd.seeepd)
								set = &epdset;
						}
						else
						{
							if (idata->pl_epd.seenrg == SEE_ALL ||
							    ( idata->pl_epd.seenrg == SEE_TEAM &&
							      p->p_freq == i->p_freq ) ||
							    ( idata->pl_epd.seenrg == SEE_SPEC &&
							      idata->speccing == p ))
								set = &nrgset;
						}
						LLAdd(set, i);
					}
				}
			pd->Unlock();

			net->SendToSet(&regset,
					(byte*)&sendpos,
					sizeof(struct S2CPosition) - sizeof(struct ExtraPosData),
					nflags);
			net->SendToSet(&nrgset,
					(byte*)&sendpos,
					sizeof(struct S2CPosition) - sizeof(struct ExtraPosData) + 2,
					nflags);
			net->SendToSet(&epdset,
					(byte*)&sendpos,
					sizeof(struct S2CPosition),
					nflags);
		}
		LLEmpty(&regset);
		LLEmpty(&nrgset);
		LLEmpty(&epdset);
	}

	/* lag data */
	if (lagc)
		lagc->Position(
				p,
				TICK_DIFF(gtc, pos->time) * 10,
				n >= 26 ? pos->extra.s2cping * 10 : -1,
				data->wpnsent);

	if ((pos->status ^ data->pos.status) & STATUS_SAFEZONE)
		DO_CBS(CB_SAFEZONE, arena, SafeZoneFunc, (p, pos->x, pos->y, pos->status & STATUS_SAFEZONE));

	/* copy the whole thing. this will copy the epd, or, if the client
	 * didn't send any epd, it will copy zeros because the buffer was
	 * zeroed before data was recvd into it. */
	memcpy(&data->pos, p2, sizeof(data->pos));

	/* update position in global players array */
	p->position.x = pos->x;
	p->position.y = pos->y;
	p->position.xspeed = pos->xspeed;
	p->position.yspeed = pos->yspeed;
	p->position.rotation = pos->rotation;
	p->position.bounty = pos->bounty;
	p->position.status = pos->status;

	p->flags.sent_ppk = 1;
}


void FakePosition(Player *p, struct C2SPosition *pos, int len)
{
	Pppk(p, (byte*)pos, len);
}


void PSpecRequest(Player *p, byte *pkt, int n)
{
	pdata *data = PPDATA(p, pdkey);
	int pid2 = ((struct SimplePacket*)pkt)->d1;

	if (pid2 >= 0)
	{
		Player *p2 = pd->PidToPlayer(pid2);
		if (p->p_ship == SPEC && p2 && p2->status == S_PLAYING)
			data->speccing = p2;
	}
	else
		data->speccing = NULL;
}


local void reset_during_change(Player *p, int success, void *dummy)
{
	pd->LockPlayer(p);
	p->flags.during_change = 0;
	pd->UnlockPlayer(p);
}


void SetFreqAndShip(Player *p, int ship, int freq)
{
	pdata *data = PPDATA(p, pdkey);
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, p->pid, freq };
	Arena *arena;

	arena = p->arena;

	pd->LockPlayer(p);

	if (p->p_ship == ship &&
	    p->p_freq == freq)
	{
		/* nothing to do */
		pd->UnlockPlayer(p);
		return;
	}

	if (IS_STANDARD(p))
		p->flags.during_change = 1;
	p->p_ship = ship;
	data->speccing = NULL;
	p->p_freq = freq;

	pd->UnlockPlayer(p);

	/* send it to him, with a callback */
	if (IS_STANDARD(p))
		net->SendWithCallback(p, (byte*)&to, 6, reset_during_change, NULL);
	/* sent it to everyone else */
	net->SendToArena(arena, p, (byte*)&to, 6, NET_RELIABLE);
	if (chatnet)
		chatnet->SendToArena(arena, NULL, "SHIPFREQCHANGE:%s:%d:%d",
				p->name, p->p_ship, p->p_freq);

	DO_CBS(CB_SHIPCHANGE, arena, ShipChangeFunc,
			(p, ship, freq));

	lm->LogP(L_DRIVEL, "game", p, "changed ship/freq to ship %d, freq %d",
			ship, freq);
}

void SetShip(Player *p, int ship)
{
	SetFreqAndShip(p, ship, p->p_freq);
}

void PSetShip(Player *p, byte *pkt, int n)
{
	pdata *data = PPDATA(p, pdkey);
	Arena *arena = p->arena;
	int ship = pkt[1], freq = p->p_freq;
	Ifreqman *fm;
	int d;

	if (!arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "ship request from bad arena");
		return;
	}

	if (ship < WARBIRD || ship > SPEC)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad ship number: %d", ship);
		return;
	}

	if (p->flags.during_change)
	{
		lm->LogP(L_MALICIOUS, "game", p, "ship request before ack from previous change");
		return;
	}

	/* exponential decay by 1/2 every 10 seconds */
	d = (current_ticks() - data->changes.lastcheck) / 1000;
	data->changes.changes >>= d;
	data->changes.lastcheck += d * 1000;
	if (data->changes.changes > cfg_changelimit && cfg_changelimit > 0)
	{
		lm->LogP(L_INFO, "game", p, "too many ship changes");
		/* disable for at least 30 seconds */
		data->changes.changes |= (cfg_changelimit<<3);
		if (chat)
			chat->SendMessage(p, "You're changing ships too often, disabling for 30 seconds.");
		return;
	}
	data->changes.changes++;

	/* checked lock state */
	if (data->lockship && !(capman && capman->HasCapability(p, "bypasslock")))
	{
		if (chat)
			chat->SendMessage(p, "You have been locked in %s.",
					(p->p_ship == SPEC) ? "spectator mode" : "your ship");
		return;
	}

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		fm->ShipChange(p, &ship, &freq);
		mm->ReleaseInterface(fm);
	}

	SetFreqAndShip(p, ship, freq);
}


void SetFreq(Player *p, int freq)
{
	struct SimplePacket to = { S2C_FREQCHANGE, p->pid, freq, -1};
	Arena *arena = p->arena;

	pd->LockPlayer(p);

	if (p->p_freq == freq)
	{
		pd->UnlockPlayer(p);
		return;
	}

	if (IS_STANDARD(p))
		p->flags.during_change = 1;
	p->p_freq = freq;

	pd->UnlockPlayer(p);

	/* him, with callback */
	if (IS_STANDARD(p))
		net->SendWithCallback(p, (byte*)&to, 6, reset_during_change, NULL);
	/* everyone else */
	net->SendToArena(arena, p, (byte*)&to, 6, NET_RELIABLE);
	if (chatnet)
		chatnet->SendToArena(arena, NULL, "SHIPFREQCHANGE:%s:%d:%d",
				p->name, p->p_ship, p->p_freq);

	DO_CBS(CB_FREQCHANGE, arena, FreqChangeFunc, (p, freq));

	lm->LogP(L_DRIVEL, "game", p, "changed freq to %d", freq);
}

void PSetFreq(Player *p, byte *pkt, int n)
{
	int freq, ship;
	Arena *arena;
	Ifreqman *fm;

	arena = p->arena;
	freq = ((struct SimplePacket*)pkt)->d1;
	ship = p->p_ship;

	if (!arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change from bad arena");
		return;
	}

	if (p->flags.during_change)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change before ack from previous change");
		return;
	}

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		fm->FreqChange(p, &ship, &freq);
		mm->ReleaseInterface(fm);
	}

	if (ship == p->p_ship)
		SetFreq(p, freq);
	else
		SetFreqAndShip(p, ship, freq);
}


void MChangeFreq(Player *p, const char *line)
{
	int freq, ship;
	Arena *arena;
	Ifreqman *fm;

	arena = p->arena;
	freq = strtol(line, NULL, 0);
	ship = p->p_ship;

	if (!arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change from bad arena");
		return;
	}

	if (ship != SPEC)
		return;

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		fm->FreqChange(p, &ship, &freq);
		mm->ReleaseInterface(fm);
	}

	if (ship == p->p_ship)
		SetFreq(p, freq);
	else
		lm->LogP(L_WARN, "game", p, "freqman forced chat client into playing ship");
}


local void notify_kill(Player *killer, Player *killed, int bty, int flags, int green, int rel)
{
	struct KillPacket kp = { S2C_KILL, green, killer->pid, killed->pid, bty, flags };
	net->SendToArena(killer->arena, NULL, (byte*)&kp, sizeof(kp), rel);
	if (chatnet)
		chatnet->SendToArena(killer->arena, NULL, "KILL:%s:%s:%d:%d",
				killer->name, killed->name, bty, flags);
}

void PDie(Player *p, byte *pkt, int n)
{
	struct SimplePacket *dead = (struct SimplePacket*)pkt;
	int bty = dead->d2;
	int flagcount, reldeaths, green, killreward;
	Arena *arena = p->arena;
	Player *killer;

	if (!arena) return;

	killer = pd->PidToPlayer(dead->d1);
	if (!killer || killer->status != S_PLAYING)
	{
		lm->LogP(L_MALICIOUS, "game", p, "reported kill by bad pid %d", dead->d1);
		return;
	}

	{
		Iclientset *cset = mm->GetInterface(I_CLIENTSET, arena);
		green = cset ? cset->GetRandomPrize(arena) : 0;
		mm->ReleaseInterface(cset);
	}

	if (flags)
		flagcount = flags->GetCarriedFlags(p);
	else
		flagcount = 0;

	if (p->p_freq == killer->p_freq)
		bty = 0;

	reldeaths = cfg->GetInt(arena->cfg, "Misc", "ReliableKills", 1);

	/* cfghelp: Kill:FixedKillReward, arena, int, def: -1
	 * If this is set (to a value other than -1), specifies a fixed
	 * number of points to give for each kill (instead of the bounty. */
	killreward = cfg->GetInt(arena->cfg, "Kill", "FixedKillReward", -1);

	notify_kill(killer, p, (killreward != -1) ? killreward : bty, flagcount, green, reldeaths);

	lm->Log(L_DRIVEL, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d)",
			arena->name,
			p->name,
			killer->name,
			bty,
			flagcount);

	/* call callbacks */
	DO_CBS(CB_KILL, arena, KillFunc,
			(arena, killer, p, bty, flagcount));
}

void FakeKill(Player *killer, Player *killed, int bounty, int flags)
{
	notify_kill(killer, killed, bounty, flags, 0, TRUE);
}


void PGreen(Player *p, byte *pkt, int n)
{
	struct GreenPacket *g = (struct GreenPacket *)pkt;
	Arena *arena = p->arena;
	adata *ad;

	if (!arena) return;
	ad = P_ARENA_DATA(arena, adkey);

	/* don't forward non-shared prizes */
	if (g->green == PRIZE_THOR  && (ad->personalgreen & (1<<personal_thor))) return;
	if (g->green == PRIZE_BURST && (ad->personalgreen & (1<<personal_burst))) return;
	if (g->green == PRIZE_BRICK && (ad->personalgreen & (1<<personal_brick))) return;

	g->pid = p->pid;
	g->type = S2C_GREEN; /* HACK :) */
	net->SendToArena(arena, p, pkt, sizeof(struct GreenPacket), NET_UNRELIABLE);
	g->type = C2S_GREEN;
}


void PAttach(Player *p, byte *pkt2, int n)
{
	int pid2 = ((struct SimplePacket*)pkt2)->d1;
	Arena *arena = p->arena;
	struct SimplePacket pkt = { S2C_TURRET, p->pid, pid2 };
	Player *to;

	if (!arena) return;

	to = (pid2 == -1) ? NULL : pd->PidToPlayer(pid2);

	if (to && to->status != S_PLAYING)
	{
		lm->LogP(L_MALICIOUS, "game", p, "tried to attach to bad pid %d", pid2);
		return;
	}

	if (pid2 == -1 ||
	    ( p->arena == to->arena &&
	      p->p_freq  == to->p_freq) )
	{
		p->p_attached = pid2;
		net->SendToArena(arena, NULL, (byte*)&pkt, 5, NET_RELIABLE);
	}
}


void PKickoff(Player *p, byte *pkt2, int len)
{
	struct SimplePacket pkt = { S2C_TURRETKICKOFF, p->pid };

	if (p->status == S_PLAYING)
		net->SendToArena(p->arena, NULL, (byte*)&pkt, 3, NET_RELIABLE);
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


void PlayerAction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey), *idata;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (action == PA_PREENTERARENA)
	{
		data->lockship = ad->initlockship;
		if (ad->initspec)
		{
			p->p_ship = SPEC;
			p->p_freq = arena->specfreq;
		}
	}
	else if (action == PA_ENTERARENA)
	{
		int seenrg = SEE_NONE, seenrgspec = SEE_NONE, seeepd = SEE_NONE;

		if (ad->all_nrg)  seenrg = ad->all_nrg;
		if (ad->spec_nrg) seenrgspec = ad->spec_nrg;
		if (ad->spec_epd) seeepd = TRUE;
		if (capman && capman->HasCapability(p, "seenrg"))
			seenrg = seenrgspec = SEE_ALL;
		if (capman && capman->HasCapability(p, "seeepd"))
			seeepd = TRUE;

		data->pl_epd.seenrg = seenrg;
		data->pl_epd.seenrgspec = seenrgspec;
		data->pl_epd.seeepd = seeepd;

		data->wpnsent = 0;

		SendOldBricks(p);
	}
	else if (action == PA_LEAVEARENA)
	{
		Link *link;
		Player *i;

		pd->Lock();
		FOR_EACH_PLAYER_P(i, idata, pdkey)
			if (idata->speccing == p)
				idata->speccing = NULL;
		pd->Unlock();

		data->speccing = NULL;
	}
}


void ArenaAction(Arena *arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		unsigned int pg = 0;
		adata *ad = P_ARENA_DATA(arena, adkey);
		brickdata *bd = P_ARENA_DATA(arena, brickkey);

		/* cfghelp: Misc:SpecSeeExtra, arena, bool, def: 1
		 * Whether spectators can see extra data for the person they're
		 * spectating. */
		ad->spec_epd =
			cfg->GetInt(arena->cfg, "Misc", "SpecSeeExtra", 1);
		/* cfghelp: Misc:SpecSeeEnergy, arena, enum, def: $SEE_NONE
		 * Whose energy levels spectators can see. The options are the
		 * same as for Misc:SeeEnergy, with one addition: $SEE_SPEC
		 * means only the player you're spectating. */
		ad->spec_nrg =
			cfg->GetInt(arena->cfg, "Misc", "SpecSeeEnergy", SEE_ALL);
		/* cfghelp: Misc:SeeEnergy, arena, enum, def: $SEE_NONE
		 * Whose energy levels everyone can see: $SEE_NONE means nobody
		 * else's, $SEE_ALL is everyone's, $SEE_TEAM is only teammates. */
		ad->all_nrg =
			cfg->GetInt(arena->cfg, "Misc", "SeeEnergy", SEE_NONE);

		/* cfghelp: Misc:DontShareThor, arena, bool, def: 0
		 * Whether Thor greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Misc", "DontShareThor", 0))
			pg |= (1<<personal_thor);
		/* cfghelp: Misc:DontShareBurst, arena, bool, def: 0
		 * Whether Burst greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Misc", "DontShareBurst", 0))
			pg |= (1<<personal_burst);
		/* cfghelp: Misc:DontShareBrick, arena, bool, def: 0
		 * Whether Brick greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Misc", "DontShareBrick", 0))
			pg |= (1<<personal_brick);

		ad->personalgreen = pg;

		pthread_mutex_init(&bd->mtx, NULL);
		LLInit(&bd->list);
		bd->cbrickid = 0;
		bd->lasttime = current_ticks();

		/* cfghelp: Brick:CountBricksAsWalls, arena, bool, def: 1
		 * Whether bricks snap to the edges of other bricks (as opposed
		 * to only snapping to walls). */
		bd->countbricksaswalls = cfg->GetInt(arena->cfg, "Brick", "CountBricksAsWalls", 1);

		if (action == AA_CREATE)
			ad->initlockship = ad->initspec = FALSE;
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


/* locking/unlocking players/arena */

local void lock_work(const Target *target, int nval, int notify, int spec)
{
	LinkedList set = LL_INITIALIZER;
	Link *l;

	pd->TargetToSet(target, &set);
	for (l = LLGetHead(&set); l; l = l->next)
	{
		Player *p = l->data;
		pdata *pdata = PPDATA(p, pdkey);

		if (spec && p->arena && p->p_ship != SPEC)
			SetFreqAndShip(p, SPEC, p->arena->specfreq);

		if (notify && pdata->lockship != nval && chat)
			chat->SendMessage(p, nval ?
					(p->p_ship == SPEC ?
					 "You have been locked to spectator mode." :
					 "You have been locked to your ship.") :
					"Your ship has been unlocked.");

		pdata->lockship = nval;
	}
	LLEmpty(&set);
}


void Lock(const Target *t, int notify, int spec)
{
	lock_work(t, TRUE, notify, spec);
}


void Unlock(const Target *t, int notify)
{
	lock_work(t, FALSE, notify, FALSE);
}


void LockArena(Arena *a, int notify, int onlyarenastate, int initial, int spec)
{
	adata *ad = P_ARENA_DATA(a, adkey);

	ad->initlockship = TRUE;
	if (!initial)
		ad->initspec = TRUE;
	if (!onlyarenastate)
	{
		Target t = { T_ARENA };
		t.u.arena = a;
		lock_work(&t, TRUE, notify, spec);
	}
}


void UnlockArena(Arena *a, int notify, int onlyarenastate)
{
	adata *ad = P_ARENA_DATA(a, adkey);

	ad->initlockship = FALSE;
	ad->initspec = FALSE;
	if (!onlyarenastate)
	{
		Target t = { T_ARENA };
		t.u.arena = a;
		lock_work(&t, FALSE, notify, FALSE);
	}
}



/* call with mutex */
local void expire_bricks(Arena *arena)
{
	ticks_t gtc, timeout;
	LinkedList *list = &((brickdata*)P_ARENA_DATA(arena, brickkey))->list;
	Link *l, *next;

	timeout = cfg->GetInt(arena->cfg, "Brick", "BrickTime", 0) + 10;

	gtc = current_ticks();
	for (l = LLGetHead(list); l; l = next)
	{
		struct S2CBrickPacket *pkt = l->data;
		next = l->next;

		if (TICK_GT(gtc, pkt->starttime + timeout))
		{
			mapdata->DoBrick(arena, 0, pkt->x1, pkt->y1, pkt->x2, pkt->y2);
			LLRemove(list, pkt);
			afree(pkt);
		}
	}
}


/* call with mutex */
local void drop_brick(Arena *arena, int freq, int x1, int y1, int x2, int y2)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	struct S2CBrickPacket *pkt = amalloc(sizeof(*pkt));

	pkt->x1 = x1; pkt->y1 = y1;
	pkt->x2 = x2; pkt->y2 = y2;
	pkt->type = S2C_BRICK;
	pkt->freq = freq;
	pkt->brickid = bd->cbrickid++;
	pkt->starttime = current_ticks();
	/* workaround for stupid priitk */
	if (pkt->starttime <= bd->lasttime)
		pkt->starttime = ++bd->lasttime;
	else
		bd->lasttime = pkt->starttime;
	LLAdd(&bd->list, pkt);

	net->SendToArena(arena, NULL, (byte*)pkt, sizeof(*pkt), NET_RELIABLE | NET_PRI_P4);
	lm->Log(L_DRIVEL, "<game> {%s} brick dropped (%d,%d)-(%d,%d) (freq=%d) (id=%d)",
			arena->name,
			x1, y1, x2, y2, freq,
			pkt->brickid);

	mapdata->DoBrick(arena, 1, x1, y1, x2, y2);
}


void DropBrick(Arena *arena, int freq, int x1, int y1, int x2, int y2)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	pthread_mutex_lock(&bd->mtx);
	expire_bricks(arena);
	drop_brick(arena, freq, x1, y1, x2, y2);
	pthread_mutex_unlock(&bd->mtx);
}


void SendOldBricks(Player *p)
{
	Arena *arena = p->arena;
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	LinkedList *list = &bd->list;
	Link *l;

	pthread_mutex_lock(&bd->mtx);

	expire_bricks(arena);

	for (l = LLGetHead(list); l; l = l->next)
	{
		struct S2CBrickPacket *pkt = (struct S2CBrickPacket*)l->data;
		net->SendToOne(p, (byte*)pkt, sizeof(*pkt), NET_RELIABLE);
	}

	pthread_mutex_unlock(&bd->mtx);
}


void PBrick(Player *p, byte *pkt, int len)
{
	int dx, dy, x1, y1, x2, y2;
	Arena *arena = p->arena;
	brickdata *bd;
	int l;

	if (!arena) return;
	bd = P_ARENA_DATA(arena, brickkey);

	/* cfghelp: Brick:BrickSpan, arena, int, def: 10
	 * The maximum length of a dropped brick. */
	l = cfg->GetInt(arena->cfg, "Brick", "BrickSpan", 10);

	dx = ((struct SimplePacket*)pkt)->d1;
	dy = ((struct SimplePacket*)pkt)->d2;

	pthread_mutex_lock(&bd->mtx);
	expire_bricks(arena);
	mapdata->FindBrickEndpoints(arena, dx, dy, l, &x1, &y1, &x2, &y2);
	drop_brick(arena, p->p_freq, x1, y1, x2, y2);
	pthread_mutex_unlock(&bd->mtx);
}


