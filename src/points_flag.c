
/* dist: public */

#include "asss.h"


/* prototypes */

local void MyFlagWin(Arena *, int);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iflags *flags;
local Istats *stats;
local Iarenaman *aman;
local Iconfig *cfg;

EXPORT int MM_points_flag(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		flags = mm->GetInterface(I_FLAGS, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(flags);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_FLAGWIN, MyFlagWin, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_FLAGWIN, MyFlagWin, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


void MyFlagWin(Arena *arena, int freq)
{
	LinkedList set = LL_INITIALIZER;
	int players, reward, splitpts, points;
	Player *i;
	Link *link;

	players = 0;
	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
		    i->arena == arena &&
		    i->p_ship != SPEC)
		{
			players++;
			if (i->p_freq == freq)
			{
				LLAdd(&set, i);
				stats->IncrementStat(i, STAT_FLAG_GAMES_WON, 1);
			}
			else
				stats->IncrementStat(i, STAT_FLAG_GAMES_LOST, 1);
		}
	pd->Unlock();

	/* cfghelp: Flag:FlagReward, arena, int, def: 5000, mod: points_flag
	 * The basic flag reward is calculated as (players in arena)^2 *
	 * reward / 1000. */
	reward = cfg->GetInt(arena->cfg, "Flag", "FlagReward", 5000);
	/* cfghelp: Flag:SplitPoints, arena, bool, def: 0
	 * Whether to split a flag reward between the members of a freq or
	 * give them each the full amount. */
	splitpts = cfg->GetInt(arena->cfg, "Flag", "SplitPoints", 0);

	points = players * players * reward / 1000;

	flags->FlagVictory(arena, freq, points);

	if (splitpts && LLCount(&set) > 0)
		points /= LLCount(&set);

	for (link = LLGetHead(&set); link; link = link->next)
		stats->IncrementStat(link->data, STAT_FLAG_POINTS, points);
	LLEmpty(&set);

	stats->SendUpdates();
}

