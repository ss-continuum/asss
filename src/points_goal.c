
/* dist: public */

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
local void MyGoal(Arena *, Player *, int, int, int);
local void MyAA(Arena *, int);
#if 0
local int  IdGoalScored(int, int, int);
#endif
local void RewardPoints(Arena *, int);
local void CheckGameOver(Arena *, int);
local void ScoreMsg(Arena *, Player *);
local void Csetscore(const char *,Player *, const Target *);
local void Cscore(const char *, Player *, const Target *);
local void Cresetgame(const char *, Player *, const Target *);
local helptext_t setscore_help, score_help, resetgame_help;

/* global data */
local int scrkey;

local Imodman *mm;
local Iplayerdata *pd;
local Iballs *balls;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Istats *stats;


EXPORT int MM_points_goal(int action, Imodman *mm_, Arena *arena)
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
		stats = mm->GetInterface(I_STATS, ALLARENAS);

		scrkey = aman->AllocateArenaData(sizeof(struct ArenaScores));
		if (scrkey == -1) return MM_FAIL;

		cmd->AddCommand("setscore",Csetscore, setscore_help);
		cmd->AddCommand("score",Cscore, score_help);
		cmd->AddCommand("resetgame",Cresetgame, resetgame_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("setscore",Csetscore);
		cmd->RemoveCommand("score",Cscore);
		cmd->RemoveCommand("resetgame",Cresetgame);
		aman->FreeArenaData(scrkey);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(stats);
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


void MyAA(Arena *arena, int action)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);

	/* FIXME: add AA_CONFCHANGED support */
	if (action == AA_CREATE)
	{
		int i, cpts;

		/* FIXME: document these settings */
		scores->mode = cfg->GetInt(arena->cfg, "Soccer", "Mode",0);
		cpts = cfg->GetInt(arena->cfg, "Soccer", "CapturePoints",1);

		if (cpts < 0)
		{
			scores->stealpts = 0;
			for(i = 0; i < MAXFREQ; i++)
				scores->score[i] = 0;
		}
		else
		{
			scores->stealpts = cpts;
			for(i = 0; i < MAXFREQ; i++)
				scores->score[i] = cpts;
		}

#if 0
		/* setup for custom mode in future */
		{
			int goalc = 0, gf, cx, cy, w, h, gf;
			const char *g;
			char goalstr[8];

			for (i = 0; i < MAXGOALS; i++)
			{
				scores->goals[i].upperleft_x = -1;
				scores->goals[i].upperleft_y = -1;
				scores->goals[i].width = -1;
				scores->goals[i].height = -1;
				scores->goals[i].goalfreq = -1;
			}

			if (scores->mode == 7)
			{
				g = goalstr;
				for(i=0;(i < MAXGOALS) && g;i++) {
					sprintf(goalstr,"Goal%d",goalc);
					g = cfg->GetStr(arena->cfg, "Soccer", goalstr);
					if (g && sscanf(g, "%d,%d,%d,%d,%d", &cx, &cy, &w, &h, &gf) == 5)
					{
						scores->goals[i].upperleft_x = cx;
						scores->goals[i].upperleft_y = cy;
						scores->goals[i].width = w;
						scores->goals[i].height = h;
						scores->goals[i].goalfreq = gf;
					}
					goalc++;
				}
			}
		}
#endif
	}
}


void MyGoal(Arena *arena, Player *p, int bid, int x, int y)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int freq = -1, nullgoal = 0, i;
	Player *j;
	LinkedList teamset = LL_INITIALIZER, nmeset = LL_INITIALIZER;
	Link *link;

	ArenaBallData *abd = balls->GetBallData(arena);

	switch (scores->mode)
	{
		case GOAL_ALL:
			freq = abd->balls[bid].freq;
			break;

		case GOAL_LEFTRIGHT:
			freq = x < 512 ? 1 : 0;
			scores->score[freq]++;
			if (scores->stealpts) scores->score[(~freq)+2]--;
			break;

		case GOAL_TOPBOTTOM:
			freq = y < 512 ? 1 : 0;
			scores->score[freq]++;
			if (scores->stealpts) scores->score[(~freq)+2]--;
			break;

		case GOAL_CORNERS_3_1:
			freq = abd->balls[bid].freq;
			if (x < 512)
				i = y < 512 ? 0 : 2;
			else
				i = y < 512 ? 1 : 3;

			if (!scores->stealpts) scores->score[freq]++;
			else if (scores->score[i])
			{
				scores->score[freq]++;
				scores->score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_CORNERS_1_3: /* only use absolute scoring, as stealpts game is pointless */
			freq = abd->balls[bid].freq;
			scores->score[freq]++;
			break;

		case GOAL_SIDES_3_1:
			freq = abd->balls[bid].freq;
			if (x < y)
				i = x < (1024-y) ? 0 : 1;
			else
				i = x < (1024-y) ? 2 : 3;

			if (!scores->stealpts) scores->score[freq]++;
			else if (scores->score[i])
			{
				scores->score[freq]++;
				scores->score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_SIDES_1_3:
			freq = abd->balls[bid].freq;
			scores->score[freq]++;
			break;
	}

	pd->Lock();
	FOR_EACH_PLAYER(j)
		if (j->status == S_PLAYING &&
		    j->arena == arena)
		{
			if (j->p_freq == freq)
				LLAdd(&teamset, j);
			else
				LLAdd(&nmeset, j);
		}
	pd->Unlock();

	chat->SendSetSoundMessage(&teamset, SOUND_GOAL, "Team Goal! by %s", p->name);
	chat->SendSetSoundMessage(&nmeset, SOUND_GOAL, "Enemy Goal! by %s", p->name);
	LLEmpty(&teamset); LLEmpty(&nmeset);
	if (nullgoal) chat->SendArenaMessage(arena,"Enemy goal had no points to give.");

	if (scores->mode)
	{
		ScoreMsg(arena, NULL);
		CheckGameOver(arena, bid);
	}

	abd->balls[bid].freq = -1;

	balls->ReleaseBallData(arena);
}


#if 0
int IdGoalScored (Arena *arena, int x, int y)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i = 0, xmin, xmax, ymin, ymax;

	for(i=0; (i < MAXGOALS) && (scores->goals[i].upperleft_x != -1); i++)
	{
		chat->SendArenaMessage(arena,"goal %d: %d,%d,%d,%d,%d",i,
			scores->goals[i].upperleft_x,
			scores->goals[i].upperleft_y,
			scores->goals[i].width,
			scores->goals[i].height,
			scores->goals[i].goalfreq);


		xmin = scores->goals[i].upperleft_x;
		xmax = xmin + scores->goals[i].width;
		ymin = scores->goals[i].upperleft_y;
		ymax = ymin + scores->goals[i].height;

		if ((x >= xmin) && (x <= xmax) && (y >= ymin) && (y <= ymax))
		{
			return scores->goals[i].goalfreq;
		}
	}

	return -1;
}
#endif


void RewardPoints(Arena *arena, int winfreq)
{
	LinkedList set = LL_INITIALIZER;
	int players = 0, points;
	/* FIXME: document this setting */
	int reward = cfg->GetInt(arena->cfg, "Soccer", "Reward", 0);
	Link *link;
	Player *i;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
		    i->arena == arena &&
		    i->p_ship != SPEC)
		{
			players++;
			if (i->p_freq == winfreq)
			{
				stats->IncrementStat(i, STAT_BALL_GAMES_WON, 1);
				/* only do reward points if not in safe zone */
				if (!(i->position.status & STATUS_SAFEZONE))
					LLAdd(&set, i);
			}
			else
				stats->IncrementStat(i, STAT_BALL_GAMES_LOST, 1);
		}
	pd->Unlock();

	if (reward < 0)
		points = reward * -1;
	else
		points = players * players * reward / 1000;

	for (link = LLGetHead(&set); link; link = link->next)
		stats->IncrementStat(link->data, STAT_FLAG_POINTS, points);
	LLEmpty(&set);

	stats->SendUpdates();
}

void CheckGameOver(Arena *arena, int bid)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, j = 0, freq = 0;

	if (cfg->GetInt(arena->cfg, "Misc", "TimedGame", 0))
		return;

	for(i = 0; i < MAXFREQ; i++)
		if (scores->score[i] > scores->score[freq]) freq = i;

	// check if game is over
	if (scores->mode <= 2 && scores->stealpts)
	{
		if (!scores->score[(~freq)+2]) // check opposite freq (either 0 or 1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = scores->stealpts;
		}
	}
	else if (scores->mode > 2 && scores->stealpts)
	{
		for (i = 0, j = 0; i < 4; i++) // check that other 3 freqs have no points
			if (!scores->score[i]) j++;

		if (j == 3)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = scores->stealpts;
		}
	}
	else // is mode 1-6 with absolute scoring
	{
		int win = cfg->GetInt(arena->cfg, "Soccer", "CapturePoints",0);
		/* FIXME: document this */
		int by  = cfg->GetInt(arena->cfg, "Soccer", "WinBy",0);

		if (scores->score[freq] >= win*-1)
			for(i = 0; i < MAXFREQ; i++)
				if ((scores->score[i]+by) <= scores->score[freq]) j++;

		if (j == MAXFREQ-1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = 0;

		}
	}

}

void ScoreMsg(Arena *arena, Player *p)  // pid = -1 means arena-wide, otherwise private
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	char _buf[256];

	strcpy(_buf,"SCORE: Warbirds:%d  Javelins:%d");
	if (scores->mode > 2)
		{
			strcat(_buf,"  Spiders:%d  Leviathans:%d");
			if (!p) chat->SendArenaMessage(arena,_buf,scores->score[0],scores->score[1],scores->score[2],scores->score[3]);
			else chat->SendMessage(p,_buf,scores->score[0],scores->score[1],scores->score[2],scores->score[3]);
		}
		else
			if (!p) chat->SendArenaMessage(arena,_buf,scores->score[0],scores->score[1]);
			else chat->SendMessage(p,_buf,scores->score[0],scores->score[1]);
}


local helptext_t setscore_help =
"Targets: none\n"
"Args: <freq 0 score> [<freq 1 score> [... [<freq 7 score>]]]\n"
"Changes score of current soccer game, based on arguments. Only supports\n"
"first eight freqs, and arena must be in absolute scoring mode \n"
"(Soccer:CapturePoints < 0).\n";

void Csetscore(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, newscores[MAXFREQ];

	/* crude for now */
	for(i = 0; i < MAXFREQ; i++)
		newscores[i] = -1;

	if (sscanf(params,"%d %d %d %d %d %d %d %d", &newscores[0], &newscores[1], &newscores[2],
		&newscores[3], &newscores[4], &newscores[5],&newscores[6], &newscores[7]) > 0)
	{
		// only allowed to setscore in modes 1-6 and if game is absolute scoring
		if (!scores->mode) return;
		if (scores->stealpts) return;

		for(i = 0; i < MAXFREQ && newscores[i] != -1; i ++)
			scores->score[i] = newscores[i];

		if (scores->mode) {
			ScoreMsg(arena, NULL);
			CheckGameOver(arena, -1);
		}
	}
	else
		chat->SendMessage(p,"setscore format: *setscore x y z .... where x = freq 0, y = 1,etc");
}


local helptext_t score_help =
"Targets: none\n"
"Args: none\n"
"Returns score of current soccer game.\n";

void Cscore(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);

	if (scores->mode) ScoreMsg(arena, p);
}


local helptext_t resetgame_help =
"Targets: none\n"
"Args: none\n"
"Resets soccer game scores and balls.\n";

void Cresetgame(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, j = 0;

	if (scores->mode)
	{
		chat->SendArenaMessage(arena, "Resetting game. -%s", p->name);
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
		balls->EndGame(arena);
		if (scores->stealpts) j = scores->stealpts;
		for(i = 0; i < MAXFREQ; i++)
			scores->score[i] = j;
	}
}

