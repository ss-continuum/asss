
/* dist: public */

#include "asss.h"


/* prototypes */

local void MyKillFunc(Arena *, Player *, Player *, int, int, int*);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Iflags *flags;

EXPORT int MM_points_kill(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		flags = mm->GetInterface(I_FLAGS, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(flags);
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
	return MM_FAIL;
}


void MyKillFunc(Arena *arena, Player *killer, Player *killed,
		int bounty, int transflags, int *totalpts)
{
	int tk, fixedreward, pts;

	tk = killer->p_freq == killed->p_freq;
	fixedreward = cfg->GetInt(arena->cfg, "Kill", "FixedKillReward", -1);
	pts = (fixedreward != -1) ? fixedreward : bounty;

	/* cfghelp: Kill:FlagMinimumBounty, arena, int, def: 0
	 * The minimum bounty the killing player must have to get any bonus
	 * kill points for flags transferred, carried or owned. */
	if (killer->position.bounty >=
	    cfg->GetInt(arena->cfg, "Kill", "FlagMinimumBounty", 0))
	{
		/* cfghelp: Kill:PointsPerKilledFlag, arena, int, def: 100
		 * The number of extra points to give for each flag a killed player
		 * was carrying. Note that the flags don't actually have to be
		 * transferred to the killer to be counted here. */
		if (transflags)
			pts += transflags *
				cfg->GetInt(arena->cfg, "Kill", "PointsPerKilledFlag", 0);

		if (flags)
		{
			/* cfghelp: Kill:PointsPerCarriedFlag, arena, int, def: 0
			 * The number of extra points to give for each flag the killing
			 * player is carrying. Note that flags that were transfered to
			 * the killer as part of the kill are counted here, so adjust
			 * PointsPerKilledFlag accordingly. */
			pts += flags->GetCarriedFlags(killer) *
				cfg->GetInt(arena->cfg, "Kill", "PointsPerCarriedFlag", 0);

			/* cfghelp: Kill:PointsPerTeamFlag, arena, int, def: 0
			 * The number of extra points to give for each flag owned by
			 * the killing team. Note that flags that were transfered to
			 * the killer as part of the kill are counted here, so
			 * adjust PointsPerKilledFlag accordingly. */
			pts += flags->GetFreqFlags(arena, killer->p_freq) *
				cfg->GetInt(arena->cfg, "Kill", "PointsPerTeamFlag", 0);
		}
	}

	/* cfghelp: Misc:TeamKillPoints, arena, bool, def: 0
	 * Whether points are awarded for a team-kill. */
	if (tk &&
	    !cfg->GetInt(arena->cfg, "Misc", "TeamKillPoints", 0))
		pts = 0;

	*totalpts += pts;
}

