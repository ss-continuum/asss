
#include <stdlib.h>

#include "asss.h"

#define WARPDIST (19<<4)

/* packet funcs */
local void Pppk(int, byte *, int);


/* global data */

local int onfor[MAXARENA];

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Imapdata *mapdata;


int MM_autowarp(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_MAPDATA, &mapdata);
		net->AddPacket(C2S_POSITION, Pppk);
		
		for (i = 0; i < MAXARENA; i++)
			onfor[i] = 0;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_MAPDATA, &mapdata);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		onfor[arena] = 1;
	}
	else if (action == MM_DETACH)
	{
		onfor[arena] = 0;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


local void DoChecksum(struct S2CWeapons *pkt, int n)
{
	int i;
	u8 ck = 0, *p = (u8*)pkt;
	for (i = 0; i < n; i++, p++)
		ck ^= *p;
	pkt->checksum = ck;
}


void Pppk(int pid, byte *p2, int n)
{
	struct C2SPosition *p = (struct C2SPosition *)p2;
	int arena = pd->players[pid].arena, warpy = 0;

	/* handle common errors */
	if (arena < 0 || !onfor[arena]) return;

	/* speccers don't get their position sent to anyone */
	if (pd->players[pid].shiptype == SPEC)
		return;

	if (p->yspeed < 0 &&
	    mapdata->InRegion(arena, "warpup", p->x>>4, p->y>>4))
		warpy = -WARPDIST;
	else if (p->yspeed > 0 &&
	         mapdata->InRegion(arena, "warpdown", p->x>>4, p->y>>4))
		warpy = WARPDIST;

	if (warpy)
	{
		struct S2CWeapons wpn = {
			S2C_WEAPON, p->rotation, p->time & 0xFFFF, p->x, p->yspeed,
			pid, p->xspeed, 0, p->status, 0, p->y + warpy, p->bounty
		};
		wpn.weapon = p->weapon;

		DoChecksum(&wpn, sizeof(struct S2CWeapons));
		net->SendToOne(pid, (byte*)&wpn, sizeof(wpn), NET_RELIABLE | NET_IMMEDIATE);
	}
}


