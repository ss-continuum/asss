
#include "asss.h"

#define MODULE "points_kill"


/* prototypes */

local void MyKillFunc(int, int, int, int, int);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Istats *stats;

int MM_basicpoints(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		WANTIFACE(I_PLAYERDATA, pd);
		WANTIFACE(I_STATS, stats);

		if (!stats) return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		FORGETIFACE(I_PLAYERDATA, pd);
		FORGETIFACE(I_STATS, stats);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CALLBACK_KILL, MyKillFunc, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CALLBACK_KILL, MyKillFunc, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


void MyKillFunc(int arena, int killer, int killed, int bounty, int flags)
{
	if (stats)
	{
		stats->IncrementScore(killer, STAT_KPOINTS, bounty + flags * 100);
		stats->IncrementScore(killer, STAT_KILLS, 1);
		stats->IncrementScore(killed, STAT_DEATHS, 1);
		stats->SendUpdates(arena);
	}
}

