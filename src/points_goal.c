
#include <string.h>
#include <stdio.h>

#include "asss.h"

#include "settings/soccer.h"


#define MAXFREQ  CFG_SOCCER_MAXFREQ
#define MAXGOALS CFG_SOCCER_MAXGOALS


/*  Soccer modes:
 *  0 - All goals are open for scoring by any freq
 *  1 - Left/Right, two goals...Scoring is basically even freqs vs odd freqs
 *  2 - Top/Bottom, same as 1 but goals oriented vertically
 *  3 - Corners/3_1, Each freq (0-3) has one goal to defend, and three to score on
 *  4 - Corners/1_3, Each freq (0-3) has three goals to defend, and one to score on*
 *  5 - Sides/3_1, Same as mode 3, but using left/right/top/bottom goals
 *  6 - Sides/1_3, Same as mode 5 (goal orientations), except uses mode 4 rules*
 *  7 - Custom (asss only), not developed yet =)
 *  * 1_3 rules:  Birds(0) take pts from Levs(3)
 *                Javs (1) take pts from Spid(2)
 *                Spids(2) take pts from Javs(1)
 *                Levs (3) take pts from Bird(0)
 *    Games must be timed I guess, since no team can acquire all 4 pts as in modes 3,5
 */


typedef struct GoalAreas
{
	int upperleft_x;        // coord of first tile in upper-left
	int upperleft_y;        // corner
	int width;              // guess
	int height;             // .. for now just a rectangular approximation =P
	int goalfreq;           // owner freq of goal
} GoalAreas;

struct ArenaScores
{
	int mode;                       // stores type of soccer game, 0-6 by default
	int stealpts;                   // 0 = absolute scoring, else = start value for each team
	int score[MAXFREQ];             // score each freq has
	GoalAreas goals[MAXGOALS];      // array of goal-defined areas for >2 goal arenas
};


/* prototypes */
local void MyGoal(int, int, int, int, int);
local void MyAA(int, int);
//local int  IdGoalScored(int, int, int);
local void CheckGameOver(int, int);
local void ScoreMsg(int, int);
local void Csetscore(const char *,int, const Target *);
local void Cscore(const char *, int, const Target *);
local void Cresetgame(const char *, int, const Target *);
local helptext_t setscore_help, score_help, resetgame_help;

/* global data */
local struct ArenaScores scores[MAXARENA];

local Imodman *mm;
local Iplayerdata *pd;
local Iballs *balls;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;


EXPORT int MM_points_goal(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		cmd->AddCommand("setscore",Csetscore, setscore_help);
		cmd->AddCommand("score",Cscore, score_help);
		cmd->AddCommand("resetgame",Cresetgame, resetgame_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		cmd->RemoveCommand("setscore",Csetscore);
		cmd->RemoveCommand("score",Cscore);
		cmd->RemoveCommand("resetgame",Cresetgame);
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
	return MM_FAIL;
}


void MyAA(int arena, int action)
{
	/* FIXME: add AA_CONFCHANGED support */
	if (action == AA_CREATE)
	{
		int i, cpts;

		scores[arena].mode = cfg->GetInt(aman->arenas[arena].cfg, "Soccer", "Mode",0);
		cpts = cfg->GetInt(aman->arenas[arena].cfg, "Soccer", "CapturePoints",1);

		if (cpts < 0)
		{
			scores[arena].stealpts = 0;
			for(i = 0; i < MAXFREQ; i++)
				scores[arena].score[i] = 0;
		}
		else
		{
			scores[arena].stealpts = cpts;
			for(i = 0; i < MAXFREQ; i++)
				scores[arena].score[i] = cpts;
		}

#if 0
		/* setup for custom mode in future */
		{
			int goalc = 0, gf, cx, cy, w, h, gf;
			const char *g;
			char goalstr[8];

			for (i = 0; i < MAXGOALS; i++)
			{
				scores[arena].goals[i].upperleft_x = -1;
				scores[arena].goals[i].upperleft_y = -1;
				scores[arena].goals[i].width = -1;
				scores[arena].goals[i].height = -1;
				scores[arena].goals[i].goalfreq = -1;
			}

			if (scores[arena].mode == 7)
			{
				g = goalstr;
				for(i=0;(i < MAXGOALS) && g;i++) {
					sprintf(goalstr,"Goal%d",goalc);
					g = cfg->GetStr(aman->arenas[arena].cfg, "Soccer", goalstr);
					if (g && sscanf(g, "%d,%d,%d,%d,%d", &cx, &cy, &w, &h, &gf) == 5)
					{
						scores[arena].goals[i].upperleft_x = cx;
						scores[arena].goals[i].upperleft_y = cy;
						scores[arena].goals[i].width = w;
						scores[arena].goals[i].height = h;
						scores[arena].goals[i].goalfreq = gf;
					}
					goalc++;
				}
			}
		}
#endif
	}
}


void MyGoal(int arena, int pid, int bid, int x, int y)
{
	int freq = -1, i, nullgoal = 0;
	int teamset[MAXPLAYERS+1], nmeset[MAXPLAYERS+1];
	int teamc = 0, nmec = 0;

	switch (scores[arena].mode)
	{
		case GOAL_ALL:
			freq = balls->balldata[arena].balls[bid].freq;
			break;

		case GOAL_LEFTRIGHT:
			freq = x < 512 ? 1 : 0;
			scores[arena].score[freq]++;
			if (scores[arena].stealpts) scores[arena].score[(~freq)+2]--;
			break;

		case GOAL_TOPBOTTOM:
			freq = y < 512 ? 1 : 0;
			scores[arena].score[freq]++;
			if (scores[arena].stealpts) scores[arena].score[(~freq)+2]--;
			break;

		case GOAL_CORNERS_3_1:
			freq = balls->balldata[arena].balls[bid].freq;
			if (x < 512)
				i = y < 512 ? 0 : 2;
			else
				i = y < 512 ? 1 : 3;

			if (!scores[arena].stealpts) scores[arena].score[freq]++;
			else if (scores[arena].score[i])
			{
				scores[arena].score[freq]++;
				scores[arena].score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_CORNERS_1_3: /* only use absolute scoring, as stealpts game is pointless */
			freq = balls->balldata[arena].balls[bid].freq;
			scores[arena].score[freq]++;
			break;

		case GOAL_SIDES_3_1:
			freq = balls->balldata[arena].balls[bid].freq;
			if (x < y)
				i = x < (1024-y) ? 0 : 1;
			else
				i = x < (1024-y) ? 2 : 3;

			if (!scores[arena].stealpts) scores[arena].score[freq]++;
			else if (scores[arena].score[i])
			{
				scores[arena].score[freq]++;
				scores[arena].score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_SIDES_1_3:
			freq = balls->balldata[arena].balls[bid].freq;
			scores[arena].score[freq]++;
			break;
	}

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
	chat->SendSetSoundMessage(teamset, SOUND_GOAL, "Team Goal! by %s", pd->players[pid].name);
	chat->SendSetSoundMessage(nmeset, SOUND_GOAL, "Enemy Goal! by %s", pd->players[pid].name);
	if (nullgoal) chat->SendArenaMessage(arena,"Enemy goal had no points to give.");

	if (scores[arena].mode)
	{
		ScoreMsg(arena, -1);
		CheckGameOver(arena, bid);
	}

	balls->balldata[arena].balls[bid].freq = -1;
}


#if 0
int IdGoalScored (int arena, int x, int y)
{
	int i = 0, xmin, xmax, ymin, ymax;

	for(i=0; (i < MAXGOALS) && (scores[arena].goals[i].upperleft_x != -1); i++)
	{
		chat->SendArenaMessage(arena,"goal %d: %d,%d,%d,%d,%d",i,
			scores[arena].goals[i].upperleft_x,
			scores[arena].goals[i].upperleft_y,
			scores[arena].goals[i].width,
			scores[arena].goals[i].height,
			scores[arena].goals[i].goalfreq);


		xmin = scores[arena].goals[i].upperleft_x;
		xmax = xmin + scores[arena].goals[i].width;
		ymin = scores[arena].goals[i].upperleft_y;
		ymax = ymin + scores[arena].goals[i].height;

		if ((x >= xmin) && (x <= xmax) && (y >= ymin) && (y <= ymax))
		{
			return scores[arena].goals[i].goalfreq;
		}
	}

	return -1;
}
#endif


void CheckGameOver(int arena, int bid)
{
	int i, j = 0, freq = 0;/* points = cfg->GetInt(aman->arenas[arena].cfg, "Soccer","Reward",0);*/

	if (cfg->GetInt(aman->arenas[arena].cfg, "Misc", "TimedGame",0))
		return;

	for(i = 0; i < MAXFREQ; i++)
		if (scores[arena].score[i] > scores[arena].score[freq]) freq = i;

	// check if game is over
	if (scores[arena].mode <= 2 && scores[arena].stealpts)
	{
		if (!scores[arena].score[(~freq)+2]) // check opposite freq (either 0 or 1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores[arena].score[i] = scores[arena].stealpts;
		}
	}
	else if (scores[arena].mode > 2 && scores[arena].stealpts)
	{
		for (i = 0, j = 0; i < 4; i++) // check that other 3 freqs have no points
			if (!scores[arena].score[i]) j++;

		if (j == 3)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores[arena].score[i] = scores[arena].stealpts;
		}
	}
	else // is mode 1-6 with absolute scoring
	{
		int win = cfg->GetInt(aman->arenas[arena].cfg, "Soccer", "CapturePoints",0);
		int by  = cfg->GetInt(aman->arenas[arena].cfg, "Soccer", "WinBy",0);

		if (scores[arena].score[freq] >= win*-1)
			for(i = 0; i < MAXFREQ; i++)
				if ((scores[arena].score[i]+by) <= scores[arena].score[freq]) j++;

		if (j == MAXFREQ-1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores[arena].score[i] = 0;

		}
	}

}

void ScoreMsg(int arena, int pid)  // pid = -1 means arena-wide, otherwise private
{
	char _buf[256];

	strcpy(_buf,"SCORE: Warbirds:%d  Javelins:%d");
	if (scores[arena].mode > 2)
		{
			strcat(_buf,"  Spiders:%d  Leviathans:%d");
			if (pid < 0) chat->SendArenaMessage(arena,_buf,scores[arena].score[0],scores[arena].score[1],scores[arena].score[2],scores[arena].score[3]);
			else chat->SendMessage(pid,_buf,scores[arena].score[0],scores[arena].score[1],scores[arena].score[2],scores[arena].score[3]);
		}
		else
			if (pid < 0) chat->SendArenaMessage(arena,_buf,scores[arena].score[0],scores[arena].score[1]);
			else chat->SendMessage(pid,_buf,scores[arena].score[0],scores[arena].score[1]);
}


local helptext_t setscore_help =
"Targets: none\n"
"Args: <freq 0 score> [<freq 1 score> [... [<freq 7 score>]]]\n"
"Changes score of current soccer game, based on arguments. Only supports\n"
"first eight freqs, and arena must be in absolute scoring mode \n"
"(Soccer:CapturePoints < 0).\n";

void Csetscore(const char *params, int pid, const Target *target)
{
	int i, newscores[MAXFREQ], arena = pd->players[pid].arena;

	/* crude for now */
	for(i = 0; i < MAXFREQ; i++)
		newscores[i] = -1;

	if (sscanf(params,"%d %d %d %d %d %d %d %d", &newscores[0], &newscores[1], &newscores[2],
		&newscores[3], &newscores[4], &newscores[5],&newscores[6], &newscores[7]) > 0)
	{
		// only allowed to setscore in modes 1-6 and if game is absolute scoring
		if (!scores[arena].mode) return;
		if (scores[arena].stealpts) return;

		for(i = 0; i < MAXFREQ && newscores[i] != -1; i ++)
			scores[arena].score[i] = newscores[i];

		if (scores[arena].mode) {
			ScoreMsg(arena, -1);
			CheckGameOver(arena, -1);
		}
	}
	else
		chat->SendMessage(pid,"setscore format: *setscore x y z .... where x = freq 0, y = 1,etc");
}


local helptext_t score_help =
"Targets: none\n"
"Args: none\n"
"Returns score of current soccer game.\n";

void Cscore(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena;

	if (scores[arena].mode) ScoreMsg(arena, pid);
}


local helptext_t resetgame_help =
"Targets: none\n"
"Args: none\n"
"Resets soccer game scores and balls.\n";

void Cresetgame(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena, i, j = 0;

	if (scores[arena].mode)
	{
		chat->SendArenaMessage(arena, "Resetting game. -%s", pd->players[pid].name);
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
		balls->EndGame(arena);
		if (scores[arena].stealpts) j = scores[arena].stealpts;
		for(i = 0; i < MAXFREQ; i++)
			scores[arena].score[i] = j;
	}
}

