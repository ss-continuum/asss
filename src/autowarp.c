
#include <stdlib.h>

#include "asss.h"

#define WARPDIST (19<<4)

struct adata
{
	int on;
	/* FIXME: add warpdist and stuff here */
};

/* packet funcs */
local void Pppk(Player *, byte *, int);

/* global data */
local int adkey;
local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Imapdata *mapdata;
local Iarenaman *aman;


EXPORT int MM_autowarp(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		if (!net || !cfg || !pd || !mapdata) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		aman->FreeArenaData(adkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(aman);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 1;
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 0;
		return MM_OK;
	}
	return MM_FAIL;
}


local void DoChecksum(struct S2CWeapons *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData); i++)
		ck ^= ((unsigned char*)pkt)[i];
	pkt->checksum = ck;
}


void Pppk(Player *p, byte *p2, int n)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	int warpy = 0;

	/* handle common errors */
	if (!arena || !ad->on) return;

	/* speccers don't get their position sent to anyone */
	if (p->p_ship == SPEC)
		return;

	if (pos->yspeed < 0 &&
	    mapdata->InRegion(arena, "warpup", pos->x>>4, pos->y>>4))
		warpy = -WARPDIST;
	else if (pos->yspeed > 0 &&
	         mapdata->InRegion(arena, "warpdown", pos->x>>4, pos->y>>4))
		warpy = WARPDIST;

	if (warpy)
	{
		struct S2CWeapons wpn = {
			S2C_WEAPON, pos->rotation, pos->time & 0xFFFF, pos->x, pos->yspeed,
			p->pid, pos->xspeed, 0, pos->status, 0, pos->y + warpy, pos->bounty
		};

		DoChecksum(&wpn);
		net->SendToOne(p, (byte*)&wpn, sizeof(wpn), NET_PRI_P4);
	}
}


