
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

#include "settings/game.h"


/* these are bit positions for the personalgreen field */
enum { personal_thor, personal_burst, personal_brick };

typedef struct
{
	struct C2SPosition pos;
	Player *speccing;
	u32 wpnsent;
	struct { int changes; unsigned lastcheck; } changes;
	/* epd/energy stuff */
	int epd_queries;
	struct { int seenrg, seenrgspec, seeepd; } pl_epd;
	/*           enum    enum        bool              */
	int lockship;
	int deathwofiring;
} pdata;

typedef struct
{
	int spec_epd, spec_nrg, all_nrg;
	/*  bool      enum      enum     */
	u32 personalgreen;
	int initlockship, initspec;
	int deathwofiring;
} adata;


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
local Iprng *prng;
local Icmdman *cmd;

/* big arrays */
local int adkey, pdkey;

local int cfg_bulletpix, cfg_wpnpix, cfg_pospix;
local int cfg_sendanti, cfg_changelimit;
local int wpnrange[WEAPONCOUNT]; /* there are 5 bits in the weapon type */
local pthread_mutex_t specmtx = PTHREAD_MUTEX_INITIALIZER;



local void do_checksum(struct S2CWeapons *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData); i++)
		ck ^= ((unsigned char*)pkt)[i];
	pkt->checksum = ck;
}


local inline long lhypot (register long dx, register long dy)
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


local void Pppk(Player *p, byte *pkt, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)pkt;
	Arena *arena = p->arena;
	pdata *data = PPDATA(p, pdkey), *idata;
	int sendwpn, x1, y1;
	int sendtoall = 0, randnum = prng->Rand();
	Player *i;
	Link *link;
	ticks_t gtc = current_ticks();
	int latency;

#ifdef CFG_RELAX_LENGTH_CHECKS
	if (len < 22)
#else
	if (len != 22 && len != 32)
#endif
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad position packet len=%i", len);
		return;
	}

	latency = TICK_DIFF(gtc, pos->time);
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
			checksum ^= pkt[left];
		if (checksum != 0)
		{
			lm->LogP(L_MALICIOUS, "game", p, "bad position packet checksum");
			return;
		}
	}

	if (pos->x == -1 && pos->y == -1)
	{
		/* position sent after death, before respawn. these aren't
		 * really useful for anything except making sure the server
		 * knows the client hasn't dropped, so just ignore them. */
		return;
	}

	/* speccers don't get their position sent to anyone */
	if (p->p_ship != SPEC)
	{
		x1 = pos->x;
		y1 = pos->y;

		/* this check should be before the weapon ignore hook */
		if (pos->weapon.type)
			p->flags.sent_wpn = 1;

		/* this is the weapons ignore hook */
		if (pos->weapon.type && prng->Rand() < p->ignoreweapons)
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
		     prng->Rand() < cfg_sendanti)
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
			/* move this field from the main packet to the extra data,
			 * in case they don't match. */
			wpn.extra.energy = pos->energy;

			if (sendtoall != 2)
				nflags = NET_UNRELIABLE | NET_DROPPABLE;
			else
				nflags = NET_RELIABLE;

			if (wpn.weapon.type == 0)
				nflags |= NET_PRI_P3;
			else
				nflags |= NET_PRI_P5;

			do_checksum(&wpn);

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
							idata->speccing == p ||
							/* send it always to turreters */
							i->p_attached == p->pid ||
							/* and send some radar packets */
							( wpn.weapon.type == W_NULL &&
							  dist <= cfg_pospix &&
							  randnum > ((double)dist / (double)cfg_pospix *
							        (RAND_MAX+1.0))) ||
							/* bots */
							i->flags.see_all_posn)
					{
						const int plainlen = sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData);
						const int nrglen = plainlen + 2;
						const int epdlen = sizeof(struct S2CWeapons);

						if (wpn.weapon.type)
							idata->wpnsent++;

						if (i->p_ship == SPEC)
						{
							if (idata->pl_epd.seeepd && idata->speccing == p)
							{
								if (len >= 32)
									net->SendToOne(i, (byte*)&wpn, epdlen, nflags);
								else
									net->SendToOne(i, (byte*)&wpn, nrglen, nflags);
							}
							else if (idata->pl_epd.seenrgspec == SEE_ALL ||
							         (idata->pl_epd.seenrgspec == SEE_TEAM &&
							          p->p_freq == i->p_freq) ||
							         (idata->pl_epd.seenrgspec == SEE_SPEC &&
							          data->speccing == p))
								net->SendToOne(i, (byte*)&wpn, nrglen, nflags);
							else
								net->SendToOne(i, (byte*)&wpn, plainlen, nflags);
						}
						else if (idata->pl_epd.seenrg == SEE_ALL ||
						         (idata->pl_epd.seenrg == SEE_TEAM &&
						          p->p_freq == i->p_freq))
							net->SendToOne(i, (byte*)&wpn, nrglen, nflags);
						else
							net->SendToOne(i, (byte*)&wpn, plainlen, nflags);
					}
				}
			pd->Unlock();
		}
		else
		{
			int nflags;
			struct S2CPosition sendpos = {
				S2C_POSITION, pos->rotation, gtc & 0xFFFF, pos->x, (u8)latency,
				(u8)pos->bounty, (u8)p->pid, pos->status, pos->yspeed, pos->y, pos->xspeed
			};
			sendpos.extra = pos->extra;
			/* move this field from the main packet to the extra data,
			 * in case they don't match. */
			sendpos.extra.energy = pos->energy;

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
						const int plainlen = sizeof(struct S2CPosition) - sizeof(struct ExtraPosData);
						const int nrglen = plainlen + 2;
						const int epdlen = sizeof(struct S2CPosition);

						if (i->p_ship == SPEC)
						{
							if (idata->pl_epd.seeepd && idata->speccing == p)
							{
								if (len >= 32)
									net->SendToOne(i, (byte*)&sendpos, epdlen, nflags);
								else
									net->SendToOne(i, (byte*)&sendpos, nrglen, nflags);
							}
							else if (idata->pl_epd.seenrgspec == SEE_ALL ||
							         (idata->pl_epd.seenrgspec == SEE_TEAM &&
							          p->p_freq == i->p_freq) ||
							         (idata->pl_epd.seenrgspec == SEE_SPEC &&
							          data->speccing == p))
								net->SendToOne(i, (byte*)&sendpos, nrglen, nflags);
							else
								net->SendToOne(i, (byte*)&sendpos, plainlen, nflags);
						}
						else if (idata->pl_epd.seenrg == SEE_ALL ||
						         (idata->pl_epd.seenrg == SEE_TEAM &&
						          p->p_freq == i->p_freq))
							net->SendToOne(i, (byte*)&sendpos, nrglen, nflags);
						else
							net->SendToOne(i, (byte*)&sendpos, plainlen, nflags);
					}
				}
			pd->Unlock();
		}
	}

	/* lag data */
	if (lagc)
		lagc->Position(
				p,
				TICK_DIFF(gtc, pos->time) * 10,
				len >= 26 ? pos->extra.s2cping * 10 : -1,
				data->wpnsent);

	if ((pos->status ^ data->pos.status) & STATUS_SAFEZONE)
		DO_CBS(CB_SAFEZONE, arena, SafeZoneFunc, (p, pos->x, pos->y, pos->status & STATUS_SAFEZONE));

	/* copy the whole thing. this will copy the epd, or, if the client
	 * didn't send any epd, it will copy zeros because the buffer was
	 * zeroed before data was recvd into it. */
	memcpy(&data->pos, pkt, sizeof(data->pos));

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


local void FakePosition(Player *p, struct C2SPosition *pos, int len)
{
	Pppk(p, (byte*)pos, len);
}


/* call with specmtx locked */
local void clear_speccing(pdata *data)
{
	if (data->speccing)
	{
		if (data->pl_epd.seeepd)
		{
			pdata *odata = PPDATA(data->speccing, pdkey);
			if (--odata->epd_queries <= 0)
			{
				byte pkt[2] = { S2C_SPECDATA, 0 };
				net->SendToOne(data->speccing, pkt, 2, NET_RELIABLE);
				odata->epd_queries = 0;
			}
		}

		data->speccing = NULL;
	}
}

/* call with specmtx locked */
local void add_speccing(pdata *data, Player *t)
{
	data->speccing = t;

	if (data->pl_epd.seeepd)
	{
		pdata *tdata = PPDATA(t, pdkey);
		if (tdata->epd_queries++ == 0)
		{
			byte pkt[2] = { S2C_SPECDATA, 1 };
			net->SendToOne(data->speccing, pkt, 2, NET_RELIABLE);
		}
	}
}


local void PSpecRequest(Player *p, byte *pkt, int len)
{
	pdata *data = PPDATA(p, pdkey);
	int tpid = ((struct SimplePacket*)pkt)->d1;

	if (len != 3)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad spec req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || p->p_ship == SPEC)
		return;

	pthread_mutex_lock(&specmtx);

	clear_speccing(data);

	if (tpid >= 0)
	{
		Player *t = pd->PidToPlayer(tpid);
		if (t && t->status == S_PLAYING && t->p_ship != SPEC && t->arena == p->arena)
			add_speccing(data, t);
	}

	pthread_mutex_unlock(&specmtx);
}


/* ?spec command */

local void send_msg_cb(const char *line, void *clos)
{
	chat->SendMessage((Player*)clos, "  %s", line);
}

local helptext_t spec_help =
"Targets: any\n"
"Args: none\n"
"Displays players spectating you. When private, displays players\n"
"spectating the target.\n";

local void Cspec(const char *params, Player *p, const Target *target)
{
	char names[500], *end = names;
	int scnt = 0;
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;
	Player *pp;
	pdata *data;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER_P(pp, data, pdkey)
		if (data->speccing == t &&
		    (!capman->HasCapability(pp, CAP_INVISIBLE_SPECTATOR) ||
		     capman->HigherThan(p, pp)))
		{
			if ((end - names) < sizeof(names) - 34)
			{
				strcpy(end, ", ");
				end += 2;
				strcpy(end, pp->name);
				end += strlen(pp->name);
			}
		}
	pd->Unlock();

	if (end != names)
	{
		char output[524];

		snprintf(output, sizeof(output),
			(scnt == 1) ? "%i spectator: %s" : "%i spectators: %s",
			scnt, names + 2);

		wrap_text(output, 80, ' ', send_msg_cb, p);
	}
	else if (p == t)
		chat->SendMessage(p, "No players spectating you.");
	else
		chat->SendMessage(p, "No players spectating %s", t->name);
}


local void reset_during_change(Player *p, int success, void *dummy)
{
	pd->LockPlayer(p);
	p->flags.during_change = 0;
	pd->UnlockPlayer(p);
}


local void SetFreqAndShip(Player *p, int ship, int freq)
{
	pdata *data = PPDATA(p, pdkey);
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, p->pid, freq };
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);

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
	p->p_freq = freq;
	pthread_mutex_lock(&specmtx);
	clear_speccing(data);
	pthread_mutex_unlock(&specmtx);

	/* reset this counter on each ship change */
	data->deathwofiring = ad->deathwofiring;

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

local void SetShip(Player *p, int ship)
{
	SetFreqAndShip(p, ship, p->p_freq);
}

local void PSetShip(Player *p, byte *pkt, int len)
{
	pdata *data = PPDATA(p, pdkey);
	Arena *arena = p->arena;
	int ship = pkt[1], freq = p->p_freq;
	Ifreqman *fm;
	int d;

	if (len != 2)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad ship req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "ship request from bad status");
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

	if (ship == p->p_ship)
	{
		lm->LogP(L_MALICIOUS, "game", p, "already in requested ship");
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


local void SetFreq(Player *p, int freq)
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

local void PSetFreq(Player *p, byte *pkt, int len)
{
	int freq, ship;
	Arena *arena;
	Ifreqman *fm;

	arena = p->arena;
	freq = ((struct SimplePacket*)pkt)->d1;
	ship = p->p_ship;

	if (len != 3)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad freq req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change from bad arena");
		return;
	}

	if (p->flags.during_change)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change before ack from previous change");
		return;
	}

	if (p->p_freq == freq)
	{
		lm->LogP(L_WARN, "game", p, "already in requested frequency");
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


local void MChangeFreq(Player *p, const char *line)
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

local void PDie(Player *p, byte *pkt, int len)
{
	struct SimplePacket *dead = (struct SimplePacket*)pkt;
	int bty = dead->d2, pts = 0;
	int flagcount, reldeaths, green;
	Arena *arena = p->arena;
	Player *killer;

	if (len != 5)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad death packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "death packet from bad state");
		return;
	}

	killer = pd->PidToPlayer(dead->d1);
	if (!killer || killer->status != S_PLAYING || killer->arena != arena)
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

	reldeaths = cfg->GetInt(arena->cfg, "Misc", "ReliableKills", 1);

	/* this will figure out how many points to send in the packet */
	DO_CBS(CB_KILL, arena, KillFunc,
			(arena, killer, p, bty, flagcount, &pts));

	notify_kill(killer, p, pts, flagcount, green, reldeaths);

	lm->Log(L_DRIVEL, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d,pts=%d)",
			arena->name,
			p->name,
			killer->name,
			bty,
			flagcount,
			pts);

	if (!p->flags.sent_wpn)
	{
		pdata *data = PPDATA(p, pdkey);
		if (data->deathwofiring-- <= 0)
		{
			lm->LogP(L_DRIVEL, "game", p, "specced for too many deaths without firing");
			SetFreqAndShip(p, SPEC, arena->specfreq);
		}
	}

	/* reset this so we can accurately check deaths without firing */
	p->flags.sent_wpn = 0;
}

local void FakeKill(Player *killer, Player *killed, int pts, int flags)
{
	notify_kill(killer, killed, pts, flags, 0, TRUE);
}


local void PGreen(Player *p, byte *pkt, int len)
{
	struct GreenPacket *g = (struct GreenPacket *)pkt;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (len != 11)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad green packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
		return;

	/* don't forward non-shared prizes */
	if (g->green == PRIZE_THOR  && (ad->personalgreen & (1<<personal_thor))) return;
	if (g->green == PRIZE_BURST && (ad->personalgreen & (1<<personal_burst))) return;
	if (g->green == PRIZE_BRICK && (ad->personalgreen & (1<<personal_brick))) return;

	g->pid = p->pid;
	g->type = S2C_GREEN; /* HACK :) */
	net->SendToArena(arena, p, pkt, sizeof(struct GreenPacket), NET_UNRELIABLE);
	g->type = C2S_GREEN;
}


local void PAttach(Player *p, byte *pkt2, int len)
{
	int pid2 = ((struct SimplePacket*)pkt2)->d1;
	Arena *arena = p->arena;

	if (len != 3)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad attach req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
		return;

	if (pid2 == -1)
	{
		Player *to = pd->PidToPlayer(pid2);
		if (!to ||
		    to->status != S_PLAYING ||
		    to == p ||
		    p->arena != to->arena ||
		    p->p_freq != to->p_freq)
		{
			lm->LogP(L_MALICIOUS, "game", p, "tried to attach to bad pid %d", pid2);
			return;
		}
	}

	/* only send it if state has changed */
	if (p->p_attached != pid2)
	{
		struct SimplePacket pkt = { S2C_TURRET, p->pid, pid2 };
		net->SendToArena(arena, NULL, (byte*)&pkt, 5, NET_RELIABLE);
		p->p_attached = pid2;
	}
}


local void PKickoff(Player *p, byte *pkt2, int len)
{
	struct SimplePacket pkt = { S2C_TURRETKICKOFF, p->pid };

	if (p->status == S_PLAYING)
		net->SendToArena(p->arena, NULL, (byte*)&pkt, 3, NET_RELIABLE);
}


local void WarpTo(const Target *target, int x, int y)
{
	struct SimplePacket wto = { S2C_WARPTO, x, y };
	net->SendToTarget(target, (byte *)&wto, 5, NET_RELIABLE | NET_URGENT);
}


local void GivePrize(const Target *target, int type, int count)
{
	struct SimplePacket prize = { S2C_PRIZERECV, (short)count, (short)type };
	net->SendToTarget(target, (byte*)&prize, 5, NET_RELIABLE);
}


local void PlayerAction(Player *p, int action, Arena *arena)
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
		data->epd_queries = 0;

		data->wpnsent = 0;
		data->deathwofiring = ad->deathwofiring;
	}
	else if (action == PA_LEAVEARENA)
	{
		Link *link;
		Player *i;

		pthread_mutex_lock(&specmtx);

		pd->Lock();
		FOR_EACH_PLAYER_P(i, idata, pdkey)
			if (idata->speccing == p)
				clear_speccing(idata);
		pd->Unlock();

		if (data->epd_queries > 0)
			lm->LogP(L_ERROR, "game", p, "epd_queries is still nonzero");

		clear_speccing(data);

		pthread_mutex_unlock(&specmtx);
	}
}


local void ArenaAction(Arena *arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		unsigned int pg = 0;
		adata *ad = P_ARENA_DATA(arena, adkey);

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

		/* cfghelp: Security:MaxDeathWithoutFiring, arena, int, def: 5
		 * The number of times a player can die without firing a weapon
		 * before being placed in spectator mode. */
		ad->deathwofiring =
			cfg->GetInt(arena->cfg, "Security", "MaxDeathWithoutFiring", 5);

		/* cfghelp: Prize:DontShareThor, arena, bool, def: 0
		 * Whether Thor greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareThor", 0))
			pg |= (1<<personal_thor);
		/* cfghelp: Prize:DontShareBurst, arena, bool, def: 0
		 * Whether Burst greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareBurst", 0))
			pg |= (1<<personal_burst);
		/* cfghelp: Prize:DontShareBrick, arena, bool, def: 0
		 * Whether Brick greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareBrick", 0))
			pg |= (1<<personal_brick);

		ad->personalgreen = pg;

		if (action == AA_CREATE)
			ad->initlockship = ad->initspec = FALSE;
	}
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


local void Lock(const Target *t, int notify, int spec)
{
	lock_work(t, TRUE, notify, spec);
}


local void Unlock(const Target *t, int notify)
{
	lock_work(t, FALSE, notify, FALSE);
}


local void LockArena(Arena *a, int notify, int onlyarenastate, int initial, int spec)
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


local void UnlockArena(Arena *a, int notify, int onlyarenastate)
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



local Igame _myint =
{
	INTERFACE_HEAD_INIT(I_GAME, "game")
	SetFreq, SetShip, SetFreqAndShip, WarpTo, GivePrize,
	Lock, Unlock, LockArena, UnlockArena,
	FakePosition, FakeKill
};


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
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!net || !cfg || !lm || !aman || !prng) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (adkey == -1 || pdkey == -1) return MM_FAIL;

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

		if (chatnet)
			chatnet->AddHandler("CHANGEFREQ", MChangeFreq);

		if (cmd)
			cmd->AddCommand("spec", Cspec, spec_help);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		if (chatnet)
			chatnet->RemoveHandler("CHANGEFREQ", MChangeFreq);
		if (cmd)
			cmd->RemoveCommand("spec", Cspec);
		net->RemovePacket(C2S_POSITION, Pppk);
		net->RemovePacket(C2S_SETSHIP, PSetShip);
		net->RemovePacket(C2S_SETFREQ, PSetFreq);
		net->RemovePacket(C2S_DIE, PDie);
		net->RemovePacket(C2S_GREEN, PGreen);
		net->RemovePacket(C2S_ATTACHTO, PAttach);
		net->RemovePacket(C2S_TURRETKICKOFF, PKickoff);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		aman->FreeArenaData(adkey);
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
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(cmd);
		return MM_OK;
	}
	return MM_FAIL;
}

