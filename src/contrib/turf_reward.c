/* ----------------------------------------------------------------------------------------------------
 *
 *	Turf Reward Module for ASSS - a very specialized version of the Turf Zone reward system
 *                             by GiGaKiLLeR <gigamon@hotmail.com>
 *
 * ----------------------------------------------------------------------------------------------------
 *
 *
 * Reward System Specs:
 *
 * -weighted flag system
 *	1. each flag has a weight, the longer a flag is held the more it is "worth"
 *	2. when a previously owned flag is tagged back before the next ding
 *		the flag's full "worth"/weight is restored
 *		ex: My team owns a flag (worth 4 WU (weight units) upon next ding).
 *			The enemy tags my flag (now worth 1 WU upon next ding),
 *			I tag the flag back to my team, flag is worth 4 WU as before. Ding.
 *			I am rewarded with 4 WU for that flag, NOT 1 WU (since I recovered my
 *			territory within the time of the ding).  If the Ding happened before I could
 *			retag the flag, the enemy would be rewarded the 1 WU and I lost my chance to
 *			reclaim the flag for full worth.
 *		todo: ability to set how many dings until a team loses the chance to recover a flag
 *
 * -scoring is not based on # of flags your team owns but rather the # of weights.
 *	The algorithm is:
 *
 *	PerCapita = # of weights per person on team = (# of weights / # ppl on the team)
 *
 *	Sum all team's PerCapita's.
 *
 *	% of Jackpot to recieve = (PerCapita / Sum of PerCapita's) * 100
 *
 *	Jackpot = # players * 200	(NOTE: linear jackpot may not be optimal)
 *					todo: add ability to set how jackpot is calculated from cfg
 *
 *	Points to be awarded = ( Jackpot * (% of jackpot to recieve/100) ) / (# ppl on team)
 *
 * -goals of this scoring method
 *	1.  The more people on a team.  The less points awarded. All other variables held constant.
 *		There is no more incentive to fill your team up to the max, but rather figure out
 *		the optimal # of players (skillwise/teamwise) to get the maximium benefit/points
 *		with respect to the amount of opposition (other teams).
 *	2.  The longer flags are held the more they are worth. (incentive to "Hold your Turf!")
 *		Territory you already own will be worth more to keep, rather than losing it while taking
 *		over a new area.
 *	3.  Lost territory can be regained at full worth if reclaimed before next ding.
 *		Additional strategy as certain areas on the map can be worth more to reclaim/attack,
 *		leaving the door wide open to the player's decision on every situation.
 *
 * ----------------------------------------------------------------------------------------------------
 */

#include "asss.h"			// necessary include to connect the module

// various other modules I will probably have to use
local Iplayerdata *playerdata;		// player data
local Iarenaman   *arenaman;		// arena manager
local Iflags      *flagsman;		// to access flag tags and info
local Iconfig     *config;		// config (for arena .cfg) services
local Istats      *stats;		// stat / score services
local Ilogman	  *logman;		// logging services
local Imainloop   *mainloop;		// main loop - for setting the turf timer
local Imapdata	  *mapdata;		// get to number of flags in each arena
local Ichat	  *chat;		// message players

// default values for cfg settings
#define MIN_PLAYERS_ON_FREQ	3	// min # of players needed on a freq for that freq to recieve reward pts
#define MIN_PLAYERS_IN_ARENA	6	// min # of players needed in the arena for anyone to recieve reward pts
#define MIN_TEAMS		2	// min # of teams needed for anyone to recieve reward pts
#define MIN_FLAGS		1	// min # of flags needed to be owned in order to recieve reward pts
#define MIN_PERCENT_FLAGS	0	// min % of flags needed to be owned by freq in order to recieve reward pts
#define MIN_WEIGHTS		1	// min # of weights needed for a team to recieve reward pts
#define MIN_PERCENT_WEIGHTS	0	// min % of weights needed to be owned by freq in order to recieve reward pts
#define MIN_PERCENT		0	// min % of weights needed to recieve an award
#define JACKPOT_MODIFIER	200	// modifies the jackpot based on how many points per player playing
					// jackpot = # players playing * pointModifier (subject to change)
#define TIMER_INITIAL		6000	// starting timer
#define TIMER_INTERVAL		6000	// subsequent timer intervals
//#define MAX_POINTS		5000	// maximum # of points a player can be awarded
//#define RECOVER_DINGS		0	// number of dings a flag will be "recoverable"
//#define WIN_PERCENT		100	// percent of weights needed for a flag game victory

//#define REWARD_PERIODIC	0	// simple periodic scoring (when you want
//#define REWARD_FIXED_PTS	1	// scoring method where all flags are worth a certain # of points
//#define REWARD_FIXED_WGT	2	// scoring method where all flags have a constant weight
//#define REWARD_STD		3	// standard weighted scoring method
//#define REWARD_STD_MULTI	4	// todo: standard reward + collection of arenas are scored together

// since this wasn't defined before, i might as well =b
#define MAXFREQ			10000	// maximum number of freqs (0-9999)
#define MAXHISTORY		10	// maximum historical data of previous rewards to keep

// easy calls for mutex
#define LOCK_STATUS(arena) \
	pthread_mutex_lock(trmtx + arena)
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock(trmtx + arena)

// to hold extra flag data for turf flags
struct TurfFlag
{
	int freq;			// freq of player that last tagged flag
	int dings;			// number of dings the flag has been owned for
	int weight;			// weight of the flag (how much it's worth)
	int taggerPID;			// id of player that tagged the flag
	// note: player may have been on another team when tag occured
	// or may have even left the game

	// todo: change old data to linked list of teams, right now only supports one record of previous info
	int oldFreq;			// previous team that owned the flag
	int oldDings;			// previous # of dings
	int oldWeight;			// previous weight of flag
	int oldTaggerPID;		// pid of player that owned the flag last
};

struct FreqInfo
{
	int numFlags;			// number of flags freq owns
	float percentFlags;		// percent of the flags owned

	long int numWeights;		// sum of weights for owned flags
	float percentWeights;		// percent of the total weights

	int numTags;			// # of flag tags
	int numRecovers;		// # of flag recoveries

	int numPlayers;			// # of players on the freq
	float perCapita;		// weights per player on freq
	float percent;			// percent of jackpot to recieve
	unsigned int numPoints;		// number of points to award to freq
};

struct TurfArena
{
	// cfg settings for turf reward
	//int reward_style;		// change reward algorithms
	int min_players_on_freq;	// min # of players needed on a freq for that freq to recieve reward pts
	int min_players_in_arena;	// min # of players needed in the arena for anyone to recieve reward pts
	int min_teams;			// min # of teams needed for anyone to recieve reward pts
	int min_flags;			// min # of flags needed to be owned by freq in order to recieve reward pts
	float min_percent_flags;	// min % of flags needed to be owned by freq in order to recieve reward pts
	int min_weights;		// min # of weights needed to be owned by freq in order to recieve reward pts
	float min_percent_weights;	// min % of weights needed to be owned by freq in order to recieve reward pts
	float min_percent;		// min percentage of jackpot needed to recieve an award
	int jackpot_modifier;		// modifies the jackpot based on how many points per player playing

	// int min_kills_arena;		// todo: minimum # of kills needed for anyone to recieve rewards
	// int min_kills_freq;		// todo: minimum # of kills needed by a freq for that freq to recieve rewards
	// int min_tags_arena;		// todo: minimum # of tags needed in arena for anyone to recieve rewards
	// int min_tags_freq;		// todo: minimum # of tags needed by a freq for that freq to recieve rewards

	// stuff for timer
	int arena;			// data for the timer to know which arena to award
	int timerChanged;		// timer settings changed for arena
	int timer_initial;		// initial timer delay
	int timer_interval;		// interval for timer to repeat

	// reward data
	int numFlags;			// number of flags on the map
	int numPlayers;			// number of people playing (not including spectators)
	int numTeams;			// number of teams (not including ones with < MinPlayersOnFreq)
	long int numWeights;		// the complete number of flag weights
	unsigned long int numPoints;	// number of points to split up
	float sumPerCapitas;		// sum of all teams percapitas

	//unsigned int numKills;	// todo: number of kills during reward interval
	//unsigned int numTags;		// todo: number of tags during reward interval
	//unsigned int numRecovers;	// todo: number of flag recoveries during reward interval

	struct TurfFlag *flags;		// pointer to array of turf flags
	struct FreqInfo *freqs;		// pointer to array of struct holding freq info
	struct FreqInfo *history[MAXHISTORY];	// array of pointers to previous freq reward data (previous dings)
};

// global (to this file) declarations
local struct TurfArena *tr;		// huge array of turf reward data for every arena
local pthread_mutex_t trmtx[MAXARENA];	// to keep things thread safe when accessing tr

// function prototypes
local void arenaAction(int arena, int action);	// arena creation and destruction, or notice cfg settings changed
local void flagTag(int arena, int pid, int fid, int oldfreq);	// does everything necessary when a flag is claimed
local int turfRewardTimer(void *dummy);	// called when a reward is due to be processed

local void loadSettings(int arena);	// helper: reads the settings for an arena from cfg
local void clearArenaData(int arena);	// helper: clears out an arena's data (not including freq and flag)
					//	note: doesn't reset numFlags
local void clearFreqData(int arena);	// helper: clears out the freq data for a particular arena
local void clearFlagData(int arena);	// helper: clears out the flag data for a particular arena
local void clearHistory(int arena);	// helper: gets rid of any existing history
local int calculateWeight(int numDings);// helper: figure out how much a flag is worth based on it's # dings
local void calculateReward(int arena);	// helper: calculates and awards players for one arena

/* TODO:
local void awardPts(int arena);		// helper: award pts to each player based on previously done calculations
local void updateFlags(int arena);	// helper: increment the numDings for all owned flags and recalculate their weights
local void flagGameReset(int arena);	// reset all flag data
local void settings(int arena)		// re-read the settings for the turf reward
*/

// the actual entrypoint into this module
EXPORT int MM_turf_reward(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)		// when the module is to be loaded
	{
		// get all of the interfaces that we are to use
		playerdata	= mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		arenaman	= mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flagsman	= mm->GetInterface(I_FLAGS, ALLARENAS);
		config		= mm->GetInterface(I_FLAGS, ALLARENAS);
		stats		= mm->GetInterface(I_STATS, ALLARENAS);
		logman		= mm->GetInterface(I_LOGMAN, ALLARENAS);
		mainloop	= mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata		= mm->GetInterface(I_MAPDATA, ALLARENAS);
		chat		= mm->GetInterface(I_CHAT, ALLARENAS);

		// if any of the interfaces are null then loading failed
		if (!playerdata || !arenaman || !flagsman || !config || !stats || !logman || !mainloop || !mapdata)
			return MM_FAIL;

		// create all necessary callbacks
		mm->RegCallback(CB_FLAGPICKUP, flagTag, ALLARENAS);	// for when a flag is tagged
		mm->RegCallback(CB_ARENAACTION, arenaAction, ALLARENAS);// for arena create & destroy, config changed

		// initialize the tr array
		{
			int x;
			tr = (struct TurfArena *)amalloc(MAXARENA * sizeof(struct TurfArena));

			for(x=0 ; x<MAXARENA ; x++)
			{
				int y;

				tr[x].min_players_on_freq  = 0;
				tr[x].min_players_in_arena = 0;
				tr[x].min_teams	   = 0;
				tr[x].min_flags	   = 0;
				tr[x].min_percent_flags    = 0;
				tr[x].min_weights	   = 0;
				tr[x].min_percent_weights  = 0;
				tr[x].min_percent	   = 0;
				tr[x].jackpot_modifier	   = 0;

				tr[x].numFlags	 = 0;
				clearArenaData(x);
				tr[x].flags	 = NULL;
				tr[x].freqs	 = NULL;

				for(y=0 ; y<MAXHISTORY ; y++)
				{
					tr[x].history[y]=NULL;
				}
				tr[x].arena = -1;
			}
		}

		// initialize the mutual exclusions
		{
			int i;
			pthread_mutexattr_t attr;
			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

			for (i=0; i < MAXARENA; i++)
				pthread_mutex_init(trmtx + i, &attr);
			pthread_mutexattr_destroy(&attr);
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)	// when the module is to be unloaded
	{
		int x;

		// get rid of ALL the timers
		mainloop->ClearTimer(turfRewardTimer, -1);

		// unregister all the callbacks
		mm->UnregCallback(CB_FLAGPICKUP, flagTag, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, arenaAction, ALLARENAS);

		// release all interfaces
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(arenaman);
		mm->ReleaseInterface(flagsman);
		mm->ReleaseInterface(config);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(mainloop);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(chat);

		// double check that all arena's sucessfully called AA_DESTROY
		for(x=0 ; x<MAXARENA ; x++)
		{
			// if there is existing flags data, discard
			if (tr[x].flags)
			{
				afree(tr[x].flags);
				tr[x].flags = NULL;
			}

			// if there is existing freqs data, discard
			if(tr[x].freqs)
			{
				afree(tr[x].freqs);
				tr[x].freqs = NULL;
			}

			//if there is existing history data, discard
			clearHistory(x);
		}

		// clear the massive array
		afree(tr);

		return MM_OK;
	}
	return MM_FAIL;
}

local void arenaAction(int arena, int action)
{
	LOCK_STATUS(arena);

	if (action == AA_CREATE || action == AA_DESTROY)
	{
		// clear the old timers for this arena
		mainloop->ClearTimer(turfRewardTimer, arena);

		// clean up any old flag reward data
		clearArenaData(arena);

		// if there is existing flags data, discard
		if (tr[arena].flags)
		{
			afree(tr[arena].flags);
			tr[arena].flags = NULL;
		}

		// if there is existing freqs data, discard
		if(tr[arena].freqs)
		{
			afree(tr[arena].freqs);
			tr[arena].freqs = NULL;
		}

		// if there is existing history data, discard
		clearHistory(arena);
	}

	if (action == AA_CREATE)
	{
		loadSettings(arena);

		tr[arena].numFlags = mapdata->GetFlagCount(arena);
		clearArenaData(arena);

		tr[arena].flags = (struct TurfFlag *)amalloc(tr[arena].numFlags * sizeof(struct TurfFlag));
		clearFlagData(arena);		// initialize all the flags

		tr[arena].freqs = (struct FreqInfo *)amalloc(MAXFREQ * sizeof(struct FreqInfo));
		clearFreqData(arena);		// intialize the data on freqs

		// set up the timer for arena
		tr[arena].arena = arena;
		tr[arena].timerChanged = 0;
		mainloop->SetTimer(turfRewardTimer, tr[arena].timer_initial, tr[arena].timer_interval, &tr[arena].arena, arena);
	}
	else if (action == AA_CONFCHANGED)
	{
		int initial   = tr[arena].timer_initial;
		int interval = tr[arena].timer_interval;
		loadSettings(arena);
		if( (initial!=tr[arena].timer_initial) || (interval!=tr[arena].timer_interval) )
			tr[arena].timerChanged = 1;	// after next ding, the timer will be changed
	}

	UNLOCK_STATUS(arena);
}

local void loadSettings(int arena)
{
	ConfigHandle c = arenaman->arenas[arena].cfg;

	tr[arena].min_players_on_freq  = config->GetInt(c, "TurfReward", "MinPlayersFreq", MIN_PLAYERS_ON_FREQ);
	tr[arena].min_players_in_arena = config->GetInt(c, "TurfReward", "MinPlayersArena", MIN_PLAYERS_IN_ARENA);
	tr[arena].min_teams	       = config->GetInt(c, "TurfReward", "MinTeams", MIN_TEAMS);
	tr[arena].min_flags	       = config->GetInt(c, "TurfReward", "MinFlags", MIN_FLAGS);
	tr[arena].min_percent_flags    = (float)config->GetInt(c, "TurfReward", "MinFlagsPercent", MIN_PERCENT_FLAGS) / 1000;
	tr[arena].min_weights	       = config->GetInt(c, "TurfReward", "MinWeights", MIN_WEIGHTS);
	tr[arena].min_percent_weights  = (float)config->GetInt(c, "TurfReward", "MinWeightsPercent", MIN_PERCENT_WEIGHTS) / 1000;
	tr[arena].min_percent	       = (float)config->GetInt(c, "TurfReward", "MinPercent", MIN_PERCENT) / 1000;
	tr[arena].jackpot_modifier     = config->GetInt(c, "TurfReward", "JackpotModifier", JACKPOT_MODIFIER);
	tr[arena].timer_initial        = config->GetInt(c, "TurfReward", "TimerInitial", TIMER_INITIAL);
	tr[arena].timer_interval       = config->GetInt(c, "TurfReward", "TimerInterval", TIMER_INTERVAL);

	// now that settings are read in, check for possible problems, adjust if necessary
	if(tr[arena].min_players_on_freq < 1)
		tr[arena].min_players_on_freq = 1;
	if(tr[arena].min_players_in_arena < 1)
		tr[arena].min_players_in_arena = 1;
	if(tr[arena].min_teams < 1)
		tr[arena].min_teams = 1;
	if(tr[arena].min_flags < 1)
		tr[arena].min_flags = 1;
	if(tr[arena].min_weights < 1)
		tr[arena].min_weights =1;
	if(tr[arena].timer_initial < 1500)
		tr[arena].timer_initial = 1500;
	if(tr[arena].timer_interval < 1500)	// safety so that user cannot overload server with dings
		tr[arena].timer_interval = 1500;
}

local void clearArenaData(int arena)
{
	tr[arena].numPlayers    = 0;
	tr[arena].numTeams      = 0;
	tr[arena].numWeights    = 0;
	tr[arena].numPoints     = 0;
	tr[arena].sumPerCapitas = 0;
}

local void clearFreqData(int arena)
{
	int x;
	for(x=0 ; x<MAXFREQ ; x++)
	{
		struct FreqInfo *ptr = &tr[arena].freqs[x];

		ptr->numFlags=0;
		ptr->percentFlags=0;
		ptr->numWeights=0;
		ptr->percentWeights=0;
		ptr->numTags=0;
		ptr->numRecovers=0;
		ptr->numPlayers=0;
		ptr->perCapita=0;
		ptr->percent=0;
		ptr->numPoints=0;
	}
}

local void clearFlagData(int arena)
{
	int x;
	for(x=0 ; x<tr[arena].numFlags ; x++)
	{
		struct TurfFlag *ptr = &tr[arena].flags[x];

		ptr->dings=-1;
		ptr->freq=-1;
		ptr->weight=0;
		ptr->taggerPID=-1;

		ptr->oldDings=-1;
		ptr->oldFreq=-1;
		ptr->oldWeight=0;
		ptr->oldTaggerPID=-1;
	}
}

local void clearHistory(int arena)
{
	int x;
	for(x=0 ; x<MAXHISTORY ; x++)
	{
		if(tr[arena].history[x])
		{
			afree(tr[arena].history[x]);
			tr[arena].history[x]=NULL;
		}
	}
}

local void flagTag(int arena, int pid, int fid, int oldfreq)
{
	int freq;
	struct TurfFlag *pTF;

	if (ARENA_BAD(arena))
	{
		logman->Log(L_MALICIOUS,
			"<turf_reward> [%s] Flag was tagged for bad/nonexistent arena",
			playerdata->players[pid].name);
		return;
	}

	LOCK_STATUS(arena);

	freq = playerdata->players[pid].freq;
	pTF = &tr[arena].flags[fid];
	if(pTF->freq==freq)
	{
		// flag was already owned by that team
		UNLOCK_STATUS(arena);
		logman->Log(L_MALICIOUS,
			"<turf_reward> {%s} [%s] Flag was tagged was already owned by player's team.",
			arenaman->arenas[arena].name,
			playerdata->players[pid].name);
		return;
	}

	if(pTF->oldFreq==freq)
	{
		// flag was reclaimed - return it to it's full worth
		pTF->freq=pTF->oldFreq;
		pTF->dings=pTF->oldDings;
		pTF->weight=pTF->oldWeight;
		pTF->taggerPID=pTF->oldTaggerPID;

		tr[arena].freqs[freq].numTags++;
		tr[arena].freqs[freq].numRecovers++;
	}
	else
	{
		// flag is newly tagged, but there may be a team that has a chance to relcaim
		pTF->freq=freq;
		pTF->dings=0;
		pTF->weight=calculateWeight(pTF->dings);
		pTF->taggerPID=pid;

		tr[arena].freqs[freq].numTags++;
	}

	UNLOCK_STATUS(arena);

	logman->Log(L_DRIVEL, "<turf_reward> {%s} [%s] Flag was tagged",
		arenaman->arenas[arena].name, playerdata->players[pid].name);
}

local int calculateWeight(int numDings)
{
	switch(numDings)
	{
	case -1: return 0;
	case 0:  return 100;
	case 1:  return 141;
	case 2:  return 173;
	case 3:  return 200;
	case 4:  return 223;
	case 5:  return 244;
	case 6:  return 264;
	case 7:  return 282;
	case 8:  return 300;
	case 9:  return 316;
	case 10: return 331;
	case 11: return 346;
	default: break;
	}
	return numDings>11 ? 400 : 0;
	/*
	if(numDings>11)
		return 400;
	return 0;
	*/
}

local int turfRewardTimer(void *arenaPtr)
{
	int *aPtr = arenaPtr;
	int arena = *aPtr;

	LOCK_STATUS(arena);

	if((tr[arena].flags) && (tr[arena].freqs))
	{
		int x;
		calculateReward(arena);
		//awardPts(arena)
		//updateFlags(arena);
		//chat->SendArenaMessage(arena,

		// reward data becomes history
		if(tr[arena].history[MAXHISTORY-1])			// if we already have the maximum # of histories
		{
			afree(tr[arena].history[MAXHISTORY-1]);		// get rid of the oldest history
		}
		for(x=MAXHISTORY-1 ; x>0 ; x--)
		{
			tr[arena].history[x]=tr[arena].history[x-1];	// move any previous reward histories back one
		}
		tr[arena].history[0]=tr[arena].freqs;			// reward now becomes most recent history

		// new freq data for next round
		tr[arena].freqs=(struct FreqInfo *)amalloc(MAXFREQ * sizeof(struct FreqInfo));
		clearFreqData(arena);		// intialize the data on freqs

		UNLOCK_STATUS(arena);

		logman->Log(L_DRIVEL, "<turf_reward> {%s} Timer Ding", arenaman->arenas[arena].name);

		// POSSIBLE TODO: send points update to everyone in arena

		if(tr[arena].timerChanged)
		{
			int initial  = tr[arena].timer_initial;
			int interval = tr[arena].timer_interval;

			tr[arena].timerChanged = 0;
			mainloop->SetTimer(turfRewardTimer, initial, interval, &tr[arena].arena, arena);

			chat->SendArenaSoundMessage(arena, SOUND_BEEP1,
				"Notice: Reward timer updated. Initial:%i Interval:%i",
				initial,
				interval);

			return 0;	// replacing this timer call with new one
		}
		return 1;	// yes we want timer called again
	}
	else
	{
		// timer function called for an arena that was destroyed
		tr[arena].arena=-1;
		arenaPtr=NULL;
		UNLOCK_STATUS(arena);
		return 0;	// dont want timer called again!!
	}
}

local void calculateReward(int arena)
{
	int x;
	int score[MAXFREQ];

	struct TurfArena *ta	= &tr[arena];
	struct FreqInfo *freqs = ta->freqs;
	struct TurfFlag *flags = ta->flags;

	clearFreqData(arena);

	ta->numPlayers=0;
	ta->numPoints=0;
	ta->numTeams=0;
	ta->numWeights=0;
	ta->sumPerCapitas=0;

	for(x=0 ; x<MAXFREQ ; x++)
		score[x]=0;

	// fill in freq data for numFlags and numWeights
	for(x=0 ; x<ta->numFlags ; x++)
	{
		struct TurfFlag *flagPtr = &flags[x];

		freqs[flagPtr->freq].numFlags++;
		freqs[flagPtr->freq].numWeights+=flagPtr->weight;
		ta->numWeights+=flagPtr->weight;
	}

	// fill in freq data for numPlayers
	for(x=0 ; x<MAXPLAYERS ; x++)
	{
		if(playerdata->players[x].arena==arena)
		{
			freqs[playerdata->players[x].freq].numPlayers++;
			ta->numPlayers++;
		}
	}

	if( ta->numPlayers < ta->min_players_in_arena )
	{
		logman->Log(L_DRIVEL,
			"<turf_reward> {%s} Not enough players in arena for rewards.  Current:%i Minimum:%i\n",
			arenaman->arenas[arena].name,
			ta->numPlayers,
			ta->min_players_in_arena);
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Reward: 0 (Not enough players for rewards)");
		clearFreqData(arena);
		return;
	}

	for(x=0 ; x<MAXFREQ ; x++)
		if( freqs[x].numPlayers > ta->min_players_on_freq )
			ta->numTeams++;

	if(ta->numTeams < ta->min_teams)
	{
		logman->Log(L_DRIVEL, "<turf_reward> {%s} Not enough teams in arena for rewards.  Current:%i Minimum:%i\n",
			arenaman->arenas[arena].name,
			ta->numTeams,
			ta->min_teams);
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Reward: 0 (Not enough teams for rewards)");
		clearFreqData(arena);
		return;
	}

	// fill in percent flags and percent weights and percapita
	for(x=0 ; x<MAXFREQ ; x++)
	{
		if( (freqs[x].numPlayers>0) && (freqs[x].numFlags>0) && (freqs[x].numWeights>0) )
		{
			freqs[x].percentFlags = ((float)freqs[x].numFlags) / ((float)ta->numFlags) * 100;
			freqs[x].percentWeights = ((float)freqs[x].numWeights) / ((float)ta->numWeights) * 100;
			freqs[x].perCapita = ((float)freqs[x].numWeights) / ((float)freqs[x].numPlayers);
			ta->sumPerCapitas+=freqs[x].perCapita;
		}
	}

	for(x=0 ; x<MAXFREQ ; x++)
	{
		if( (freqs[x].numPlayers >= ta->min_players_on_freq)
			&& (freqs[x].numFlags >= ta->min_flags)
			&& (freqs[x].numWeights >= ta->min_weights)
			&& (freqs[x].percentFlags >= ta->min_percent_flags)
			&& (freqs[x].percentWeights >= ta->min_percent_weights) )
		{
			score[x]=1;
		}
	}

	// figure out percent of jackpot team will recieve and how many points that relates to
	ta->numPoints = ta->jackpot_modifier * ta->numPlayers;
	if(ta->sumPerCapitas>0)
	{
		for(x=0 ; x<MAXFREQ ; x++)
		{
			freqs[x].percent = (freqs[x].perCapita) / (ta->sumPerCapitas) * 100;
			if(freqs[x].numPlayers>0)
			{
				freqs[x].numPoints = (int)(ta->numPoints * (freqs[x].percent/100) / freqs[x].numPlayers);
			}
		}
	}

	// increment numdings and weights for all owned flags
	for(x=0 ; x<ta->numFlags ; x++)
	{
		struct TurfFlag *flagPtr = &flags[x];

		if(flagPtr->freq!=-1)		// if owned
		{
			flagPtr->dings=(flagPtr->dings)+1;
			flagPtr->weight=calculateWeight(flagPtr->dings);

			// previous owner lost the chance of recovery
			flagPtr->oldFreq=flagPtr->freq;
			flagPtr->oldDings=flagPtr->dings;
			flagPtr->oldWeight=flagPtr->weight;
			flagPtr->oldTaggerPID=flagPtr->taggerPID;
		}
	}

	// this is where we award each player that deserves points
	playerdata->LockStatus();
	for(x=0 ; x<MAXPLAYERS ; x++)
	{
		if( playerdata->players[x].arena == arena )
		{
			// player is in arena
			if( (playerdata->players[x].shiptype == SPEC) || (playerdata->players[x].status!=S_PLAYING) )
			{
				// player is in spec/not playing
				chat->SendSoundMessage(x, SOUND_DING, "Reward: 0 (not playing)");
			}
			else if(score[playerdata->players[x].freq])
			{
				// player is on a freq that recieved points
				int points = freqs[playerdata->players[x].freq].numPoints;

				if(points>0)
					stats->IncrementStat(x, STAT_FLAG_POINTS, points);	// award player
				chat->SendSoundMessage(x, SOUND_DING, "Reward: %i", points);	// notify player
			}
			else
			{
				// player is on a freq that didn't recieve points
				chat->SendSoundMessage(x, SOUND_DING, "Reward: 0 (reward requirements not met)");
			}
		}
	}
	playerdata->UnlockStatus();
}

