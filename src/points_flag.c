
#include "asss.h"

#define MODULE "points_flag"


/* prototypes */

local void MyFlagWin(int, int);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iflags *flags;
local Istats *stats;
local Iarenaman *aman;
local Iconfig *cfg;

int MM_points_flag(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_FLAGS, &flags);
		mm->RegInterest(I_STATS, &stats);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_STATS, &stats);
		mm->UnregInterest(I_FLAGS, &flags);
		mm->UnregInterest(I_PLAYERDATA, &pd);
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
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


/*
void MyKillFunc(int arena, int killer, int killed, int bounty, int flags)
{
	if (stats)
	{
		stats->IncrementStat(killer, STAT_KPOINTS, bounty + flags * 100);
		stats->IncrementStat(killer, STAT_KILLS, 1);
		stats->IncrementStat(killed, STAT_DEATHS, 1);
		stats->SendUpdates(arena);
	}
}
*/

void MyFlagWin(int arena, int freq)
{
	int awardto[MAXPLAYERS];
	int players, ponfreq, reward, splitpts, points, i;

	players = ponfreq = 0;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].status == S_PLAYING &&
		    pd->players[i].arena == arena)
		{
			players++;
			if (pd->players[i].freq == freq)
				awardto[ponfreq++] = i;
		}
	pd->UnlockStatus();

	reward = cfg->GetInt(aman->arenas[arena].cfg, "Flag", "FlagReward", 5000);
	splitpts = cfg->GetInt(aman->arenas[arena].cfg, "Flag", "SplitPoints", 0);

	points = players * players * reward / 1000;

	flags->FlagVictory(arena, freq, points);

	if (splitpts)
		points /= ponfreq;

	for (i = 0; i < ponfreq; i++)
		stats->IncrementStat(awardto[i], STAT_FPOINTS, points);
}

