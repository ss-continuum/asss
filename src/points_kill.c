
#include "asss.h"

#define MODULE "points_kill"


/* prototypes */

local void MyKillFunc(int, int, int, int, int);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Istats *stats;

int MM_points_kill(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_STATS, &stats);

		if (!stats) return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_STATS, &stats);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_KILL, MyKillFunc, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_KILL, MyKillFunc, arena);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void MyKillFunc(int arena, int killer, int killed, int bounty, int flags)
{
	int tk, pts;

	tk = pd->players[killer].freq == pd->players[killed].freq;
	pts = bounty;

	if (flags)
		pts += flags *
			cfg->GetInt(aman->arenas[arena].cfg, "Kill", "FlagValue", 100);

	if (tk &&
	    cfg->GetInt(aman->arenas[arena].cfg, "Misc", "TeamKillPoints", 0))
		pts = 0;

	if (stats)
	{
		stats->IncrementStat(killer, STAT_KPOINTS, pts);
		stats->IncrementStat(killer, STAT_KILLS, 1);
		stats->IncrementStat(killed, STAT_DEATHS, 1);
		stats->SendUpdates();
	}
}

