
#include "asss.h"


local Iplayerdata *pd;
local Istats *stats;


local void mypa(int pid, int action, int arena)
{
	if (action == PA_ENTERARENA)
		stats->StartTimer(pid, STAT_ARENA_TOTAL_TIME);
	else if (action == PA_LEAVEARENA)
		stats->StopTimer(pid, STAT_ARENA_TOTAL_TIME);
}

local void mykill(int arena, int killer, int killed, int bounty, int flags)
{
	stats->IncrementStat(killer, STAT_KILLS, 1);
	stats->IncrementStat(killed, STAT_DEATHS, 1);

	if (pd->players[killer].freq == pd->players[killed].freq)
	{
		stats->IncrementStat(killer, STAT_TEAM_KILLS, 1);
		stats->IncrementStat(killed, STAT_TEAM_DEATHS, 1);
	}

	if (flags)
	{
		stats->IncrementStat(killer, STAT_FLAG_KILLS, 1);
		stats->IncrementStat(killed, STAT_FLAG_DEATHS, 1);
	}
}


local void myfpickup(int arena, int pid, int fid, int of, int carried)
{
	if (carried)
	{
		stats->StartTimer(pid, STAT_FLAG_CARRY_TIME);
		stats->IncrementStat(pid, STAT_FLAG_PICKUPS, 1);
	}
}

local void mydrop(int arena, int pid, int count, int neut)
{
	stats->StopTimer(pid, STAT_FLAG_CARRY_TIME);
	if (!neut)
		stats->IncrementStat(pid, STAT_FLAG_DROPS, count);
	else
		stats->IncrementStat(pid, STAT_FLAG_NEUT_DROPS, count);
}


local void mybpickup(int arena, int pid, int bid)
{
	stats->StartTimer(pid, STAT_BALL_CARRY_TIME);
	stats->IncrementStat(pid, STAT_BALL_CARRIES, 1);
}

local void mybfire(int arena, int pid, int bid)
{
	stats->StopTimer(pid, STAT_BALL_CARRY_TIME);
}

local void mygoal(int arena, int pid, int bid, int x, int y)
{
	stats->IncrementStat(pid, STAT_BALL_GOALS, 1);
}


EXPORT int MM_basicstats(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		if (!pd || !stats) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, mypa, ALLARENAS);
		mm->RegCallback(CB_KILL, mykill, ALLARENAS);

		mm->RegCallback(CB_FLAGPICKUP, myfpickup, ALLARENAS);
		mm->RegCallback(CB_FLAGDROP, mydrop, ALLARENAS);

		mm->RegCallback(CB_BALLPICKUP, mybpickup, ALLARENAS);
		mm->RegCallback(CB_BALLFIRE, mybfire, ALLARENAS);
		mm->RegCallback(CB_GOAL, mygoal, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, mypa, ALLARENAS);
		mm->UnregCallback(CB_KILL, mykill, ALLARENAS);
		mm->UnregCallback(CB_FLAGPICKUP, myfpickup, ALLARENAS);
		mm->UnregCallback(CB_FLAGDROP, mydrop, ALLARENAS);
		mm->UnregCallback(CB_BALLPICKUP, mybpickup, ALLARENAS);
		mm->UnregCallback(CB_BALLFIRE, mybfire, ALLARENAS);
		mm->UnregCallback(CB_GOAL, mygoal, ALLARENAS);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	return MM_FAIL;
}

