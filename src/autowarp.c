
#include <stdlib.h>

#include "asss.h"

#define WARPDIST (19<<4)

/* packet funcs */
local void Pppk(int, byte *, int);


/* global data */

local int onfor[MAXARENA];

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Imapdata *mapdata;


EXPORT int MM_autowarp(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		if (!net || !cfg || !pd || !mapdata) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		for (i = 0; i < MAXARENA; i++)
			onfor[i] = 0;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
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
	return MM_FAIL;
}


local void DoChecksum(struct S2CWeapons *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < 21; i++)
		ck ^= ((unsigned char*)pkt)[i];
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

		DoChecksum(&wpn);
		net->SendToOne(pid, (byte*)&wpn, sizeof(wpn), NET_RELIABLE | NET_PRI_P4);
	}
}


