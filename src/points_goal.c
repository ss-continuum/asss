
#include "asss.h"

#include "settings/soccer.h"


#define MODULE "points_goal"

#define MAXFREQ 8


struct ArenaScores
{
	int score[MAXFREQ];
};

/* prototypes */

local void MyGoal(int, int, int, int, int);
local void MyAA(int, int);

/* global data */

local struct ArenaScores scores[MAXARENA];

local Imodman *mm;
local Iplayerdata *pd;
local Iballs *balls;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;


EXPORT int MM_points_goal(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_BALLS, &balls);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_CHAT, &chat);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_BALLS, &balls);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_ARENAACTION, MyAA, arena);
		mm->RegCallback(CB_GOAL, MyGoal, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_GOAL, MyGoal, arena);
		mm->UnregCallback(CB_ARENAACTION, MyAA, arena);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void MyAA(int arena, int action)
{
	if (action == AA_CREATE)
	{
		int i;
		for (i = 0; i < 2; i++)
			scores[arena].score[i] = 0;
	}
}


void MyGoal(int arena, int pid, int bid, int x, int y)
{
	int mode, freq = -1, i;
	int teamset[MAXPLAYERS], nmeset[MAXPLAYERS];
	int teamc = 0, nmec = 0;

	mode = cfg->GetInt(aman->arenas[arena].cfg, "Soccer", "Mode", 0);

	switch (mode)
	{
		case GOAL_ALL:
			freq = pd->players[pid].freq;
			break;

		case GOAL_LEFTRIGHT:
			freq = x < 512 ? 1 : 0;
			break;

		case GOAL_TOPBOTTOM:
			freq = y < 512 ? 1 : 0;
			break;

		case GOAL_QUADRENTS_3_1:
		case GOAL_QUADRENTS_1_3:
		case GOAL_WEDGES_3_1:
		case GOAL_WEDGES_1_3:
			/* not implemented */
			break;
	}

	if (freq >= 0 && freq < MAXFREQ)
		scores[arena].score[freq]++;

	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].status == S_PLAYING &&
		    pd->players[i].arena == arena)
		{
			if (pd->players[i].freq == freq)
				teamset[teamc++] = i;
			else
				nmeset[nmec++] = i;
		}
	pd->UnlockStatus();

	teamset[teamc] = nmeset[nmec] = -1;
	chat->SendSetSoundMessage(teamset, SOUND_GOAL, "Team Goal! by %s  Reward:1", pd->players[pid].name);
	chat->SendSetSoundMessage(nmeset, SOUND_GOAL, "Enemy Goal! by %s  Reward:1", pd->players[pid].name);
}


