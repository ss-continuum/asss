/* ----------------------------------------------------------------------------------------------------
 *
 *      Turf Reward Module for ASSS - a very specialized version of the Turf Zone reward system
 *                             by GiGaKiLLeR <gigamon@hotmail.com>
 *
 * ----------------------------------------------------------------------------------------------------
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
local Icmdman	  *cmdman;		// for command handling

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
#define RECOVER_DINGS		0	// number of dings a flag will be "recoverable"
//#define WIN_PERCENT		100	// percent of weights needed for a flag game victory
//#define WIN_RESET		0	// reset flags on flag game victory?

// bring in the settings for reward types
#include "settings/turfreward.h"

// since this wasn't defined before, i might as well =b
#define MAXFREQ		10000	// maximum number of freqs (0-9999)
#define MAXHISTORY		10	// maximum historical data of previous rewards to keep

// easy calls for mutex
#define LOCK_STATUS(arena) \
	pthread_mutex_lock(trmtx + arena)
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock(trmtx + arena)


// for linked list for data on teams that have a chance to 'recover'
struct OldNode
{
	int lastOwned;			// how many dings ago the flag was owned

	int freq;			// previous team that owned the flag
	int dings;			// previous # of dings
	int weight;			// previous weight of flag
	int taggerPID;			// pid of player that owned the flag last
};

// to hold extra flag data for turf flags
struct TurfFlag
{
	int freq;			// freq of player that last tagged flag
	int dings;			// number of dings the flag has been owned for
	int weight;			// weight of the flag (how much it's worth)
	int taggerPID;			// id of player that tagged the flag
					// note: player may have been on another team when tag occured
					// or may have even left the game

	LinkedList old;
};

struct FreqInfo
{
	int numFlags;			// number of flags freq owns
	double percentFlags;		// percent of the flags owned

	long int numWeights;		// sum of weights for owned flags
	double percentWeights;		// percent of the total weights

	int numTags;			// # of flag tags
	int numRecovers;		// # of flag recoveries
	int numLost;			// # of flags lost (possibly have chance to recover based on settings)

	int numPlayers;			// # of players on the freq
	double perCapita;		// weights per player on freq
	double percent;			// percent of jackpot to recieve
	unsigned int numPoints;		// number of points to award to freq
};

struct TurfArena
{
	// cfg settings for turf reward
	int reward_style;		// change reward algorithms
	int min_players_on_freq;	// min # of players needed on a freq for that freq to recieve reward pts
	int min_players_in_arena;	// min # of players needed in the arena for anyone to recieve reward pts
	int min_teams;			// min # of teams needed for anyone to recieve reward pts
	int min_flags;			// min # of flags needed to be owned by freq in order to recieve reward pts
	double min_percent_flags;	// min % of flags needed to be owned by freq in order to recieve reward pts
	int min_weights;		// min # of weights needed to be owned by freq in order to recieve reward pts
	double min_percent_weights;	// min % of weights needed to be owned by freq in order to recieve reward pts
	double min_percent;		// min percentage of jackpot needed to recieve an award
	int jackpot_modifier;		// modifies the jackpot based on how many points per player playing
	int recover_dings;
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
	double sumPerCapitas;		// sum of all teams percapitas

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
local void flagGameReset(int arena);	// helper: reset all flag data
local int calculateWeight(int numDings);// helper: figure out how much a flag is worth based on it's # dings

local void crStandard(int arena);	// helper: calculates points using standard algorithm
local void crPeriodic(int arena);	// helper: calculates points using the periodic algorithm (use when want stats)

local void awardPts(int arena);		// helper: award pts to each player based on previously done calculations
local void updateFlags(int arena);	// helper: increment the numDings for all owned flags and recalculate their weights

// standard user commands
//local void C_turfTime(const char *, int, const Target *);	// to find out how much time till next ding
//local void C_turfStats(const char *, int, const Target *);	// to get stats of last ding on a certain team/player
//local void C_turfInfo(const char *, int, const Target *);	// to get settings info on minimum requirements, etc

// mod commands
local void C_turfResetFlags(const char *, int, const Target *);	// to reset the flag data on all flags
//local void C_turfResetTimer(const char *, int, const Target *);	// to reset the timer
local void C_forceDing(const char *, int, const Target *);	// to force a ding to occur, does not change the timer
//local void C_forceStats(const char *, int, const Target *);	// to force stats to be outputted on the spot

local helptext_t turfresetflags_help, forceding_help;

/* TODO:

local void flagTimerReset(int arena);
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
		cmdman		= mm->GetInterface(I_CMDMAN, ALLARENAS);

		// if any of the interfaces are null then loading failed
		if (!playerdata || !arenaman || !flagsman || !config || !stats || !logman || !mainloop || !mapdata || !chat || !cmdman)
			return MM_FAIL;

		// create all necessary callbacks
		mm->RegCallback(CB_FLAGPICKUP, flagTag, ALLARENAS);	// for when a flag is tagged
		mm->RegCallback(CB_ARENAACTION, arenaAction, ALLARENAS);// for arena create & destroy, config changed

		// special turf_reward commands
		cmdman->AddCommand("forceding", C_forceDing, forceding_help);
		cmdman->AddCommand("turfresetflags", C_turfResetFlags, turfresetflags_help);

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

		// get rid of turf_reward commands
		cmdman->RemoveCommand("forceding", C_forceDing);
		cmdman->RemoveCommand("turfresetflags", C_turfResetFlags);

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
		mm->ReleaseInterface(cmdman);

		// double check that all arena's sucessfully called AA_DESTROY
		for(x=0 ; x<MAXARENA ; x++)
		{
			// if there is existing flags data, discard
			if (tr[x].flags)
			{
				clearFlagData(x);
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
		// clear old timer
		mainloop->ClearTimer(turfRewardTimer, arena);

		// clean up any old flag reward data
		clearArenaData(arena);

		// if there is existing flags data, discard
		if (tr[arena].flags)
		{
			clearFlagData(arena);
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
		int initial  = tr[arena].timer_initial;
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

	tr[arena].reward_style = REWARD_STD;
	tr[arena].min_players_on_freq  = config->GetInt(c, "TurfReward", "MinPlayersFreq", MIN_PLAYERS_ON_FREQ);
	tr[arena].min_players_in_arena = config->GetInt(c, "TurfReward", "MinPlayersArena", MIN_PLAYERS_IN_ARENA);
	tr[arena].min_teams	       = config->GetInt(c, "TurfReward", "MinTeams", MIN_TEAMS);
	tr[arena].min_flags	       = config->GetInt(c, "TurfReward", "MinFlags", MIN_FLAGS);
	tr[arena].min_percent_flags    = (double)config->GetInt(c, "TurfReward", "MinFlagsPercent", MIN_PERCENT_FLAGS) / 1000.0;
	tr[arena].min_weights	       = config->GetInt(c, "TurfReward", "MinWeights", MIN_WEIGHTS);
	tr[arena].min_percent_weights  = (double)config->GetInt(c, "TurfReward", "MinWeightsPercent", MIN_PERCENT_WEIGHTS) / 1000.0;
	tr[arena].min_percent	       = (double)config->GetInt(c, "TurfReward", "MinPercent", MIN_PERCENT) / 1000.0;
	tr[arena].jackpot_modifier     = config->GetInt(c, "TurfReward", "JackpotModifier", JACKPOT_MODIFIER);
	tr[arena].timer_initial        = config->GetInt(c, "TurfReward", "TimerInitial", TIMER_INITIAL);
	tr[arena].timer_interval       = config->GetInt(c, "TurfReward", "TimerInterval", TIMER_INTERVAL);
	tr[arena].recover_dings	       = config->GetInt(c, "TurfReward", "RecoverDings", RECOVER_DINGS);

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
		ptr->numLost=0;
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

		// now clear out the linked list 'old'
		LLEnum(&ptr->old, afree);
		LLEmpty(&ptr->old);
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

local void flagGameReset(int arena)
{
	clearFlagData(arena);
}

local void flagTag(int arena, int pid, int fid, int oldfreq)
{
	int freq;
	struct TurfFlag *pTF;
	struct OldNode *oPtr;

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

	// increment number of tags for freq
	tr[arena].freqs[freq].numTags++;

	if(!LLIsEmpty(&pTF->old))
	{
		Link *l;

		oPtr = NULL;

		// list of teams that have chance to recover exists, we have to check if freq is one of these
		// search through linked list for matching freq
		for(l = LLGetHead(&pTF->old); l; l = l->next)
		{
			oPtr = l->data;

			if(oPtr->freq == freq)
			{
				// found entry that matches freq, meaning flag was recovered
				int oFreq = pTF->freq;
				int oDings = pTF->dings;
				int oWeight = pTF->weight;
				int oPID = pTF->taggerPID;

				// restore flag data
				pTF->freq = oPtr->freq;
				pTF->dings = oPtr->dings;
				pTF->weight = oPtr->weight;
				pTF->taggerPID = pid;	// pid of player who recovered flag now gets taggerPID

				if(oFreq!=-1)
				{
					// team that owned flag has chance to recover (reuse allocated memory)
					oPtr->lastOwned = 0;	// just lost the flag
					oPtr->freq = oFreq;
					oPtr->dings = oDings;
					oPtr->weight = oWeight;
					oPtr->taggerPID = oPID;

					// increment number of lost flags for freq that lost it
					tr[arena].freqs[oFreq].numLost++;
				}

				// increment number of recoveries for freq that recovered flag
				tr[arena].freqs[freq].numRecovers++;

				break;	// end for loop
			}
		}

		if(!oPtr)
		{
			// didn't find a matching entry in linked list for freq
			if(pTF->freq!=-1)
			{
				// flag was owned by a team, now they have a chance to recover
				oPtr = (struct OldNode *)amalloc(sizeof(struct OldNode));
				oPtr->lastOwned = 0;
				oPtr->freq	= pTF->freq;
				oPtr->dings	= pTF->dings;
				oPtr->weight	= pTF->weight;
				oPtr->taggerPID = pTF->taggerPID;

				// tack node onto front of linked list
				LLAddFirst(&pTF->old, oPtr);

				// increment number of flag losses for freq that lost it
				tr[arena].freqs[oPtr->freq].numLost++;

				// fill in data for newly tagged flag
				pTF->freq=freq;
				pTF->dings=0;
				pTF->weight=calculateWeight(pTF->dings);
				pTF->taggerPID=pid;
			}
		}
	}
	else
	{
		// no teams had chance to recover
		if(pTF->freq!=-1)
		{
			// flag was owned by a team, now they have a chance to recover
			oPtr = (struct OldNode *)amalloc(sizeof(struct OldNode));
			oPtr->lastOwned = 0;
			oPtr->freq	= pTF->freq;
			oPtr->dings	= pTF->dings;
			oPtr->weight	= pTF->weight;
			oPtr->taggerPID = pTF->taggerPID;

			// tack node onto front of linked list
			LLAddFirst(&pTF->old, oPtr);

			// increment number of flag losses for freq that lost it
			tr[arena].freqs[oPtr->freq].numLost++;

			// fill in data for newly tagged flag
			pTF->freq=freq;
			pTF->dings=0;
			pTF->weight=calculateWeight(pTF->dings);
			pTF->taggerPID=pid;
		}
		else
		{
			// flag was unowed, simply fill in data for newly tagged flag
			pTF->freq=freq;
			pTF->weight=0;
			pTF->weight=calculateWeight(pTF->weight);
			pTF->taggerPID=pid;
		}
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
}

local int turfRewardTimer(void *arenaPtr)
{
	int x;
	int *aPtr = arenaPtr;
	int arena = *aPtr;

	LOCK_STATUS(arena);

	if(!(tr[arena].flags) || !(tr[arena].freqs))
	{
		// timer function called for an arena that was destroyed
		tr[arena].arena=-1;
		arenaPtr=NULL;

		UNLOCK_STATUS(arena);
		return 0;	// dont want timer called again!!
	}

	// lock the playerdata once and only once to avoid deadlock stuff
	playerdata->LockStatus();

	// calculate the points to award
	switch(tr[arena].reward_style)
	{
	case REWARD_DISABLED:
		break;
	case REWARD_PERIODIC:
		crPeriodic(arena);
		awardPts(arena);	// award the players accordingly
		break;
	case REWARD_FIXED_PTS:
		awardPts(arena);	// award the players accordingly
		break;
	case REWARD_FIXED_WGT:
		awardPts(arena);	// award the players accordingly
		break;
	case REWARD_STD:
		crStandard(arena);	// using standard scoring algorithm
		updateFlags(arena);	// update flag dings/weights
		awardPts(arena);	// award the players accordingly
		break;
	case REWARD_STD_MULTI:
		break;
	}

	// free the lock on playerdata, we're done with it
	playerdata->UnlockStatus();

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

	// check if we had any timer changes in cfg
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

local void crStandard(int arena)
{
	int x;
	int score[MAXFREQ];

	struct TurfArena *ta   = &tr[arena];
	struct FreqInfo *freqs = ta->freqs;
	struct TurfFlag *flags = ta->flags;

	clearFreqData(arena);		// make sure the scoring data is on a clean slate

	ta->numPlayers=0;
	ta->numPoints=0;
	ta->numTeams=0;
	ta->numWeights=0;
	ta->sumPerCapitas=0;

	// clear score array this will be used to tell which teams the jackpot will be split between
	for(x=0 ; x<MAXFREQ ; x++)
		score[x]=0;

	// go through all flags and if owned, updating freq info on numFlags and numWeight
	for(x=0 ; x<ta->numFlags ; x++)
	{
		int freq, dings, weight;
		struct TurfFlag *flagPtr = &flags[x];

		freq=flagPtr->freq;
		dings=flagPtr->dings;
		weight=flagPtr->weight;

		if(freq>=0 && dings>=0 && weight>=0)
		{
			// flag is owned
			freqs[freq].numFlags++;
			freqs[freq].numWeights+=weight;
			ta->numWeights+=weight;
		}
	}

	// go through all players and update freq info on numPlayers
	for(x=0 ; x<MAXPLAYERS ; x++)
	{
		struct PlayerData *pdPtr = &playerdata->players[x];
		if( (pdPtr->arena==arena) && (pdPtr->shiptype!=SPEC) && (pdPtr->status==S_PLAYING) )
		{
			freqs[pdPtr->freq].numPlayers++;
			ta->numPlayers++;
		}
	}

	/* at this point # flags, # weights, and # of players for every freq (and thus the entire arena) are recorded
	 * in order for us to figure out % flags we must be sure # Flags in arena > 0
	 * so lets make sure that map does in fact have flags to score for (numFlags for arena > 0)
	 */
	if(ta->numFlags<1)
	{
		// no flags, therefore no weights, stop right here
		logman->Log(L_WARN,
			"<turf_reward> {%s} Map has no flags.",
			arenaman->arenas[arena].name);
		return;
	}

	// in order for us to figure out % weights we must be sure that numWeights for arena > 0
	if(ta->numWeights<1)
	{
		// no team owns flags
		logman->Log(L_DRIVEL, "<turf_reward> {%s} No one owns any weights.\n",
			arenaman->arenas[arena].name,
			ta->numTeams,
			ta->min_teams);
		chat->SendArenaSoundMessage(arena, SOUND_BEEP1, "Notice: all flags are unowned.");
		return;
	}

	// fill in % flags and % weights
	for(x=0 ; x<MAXFREQ ; x++)
	{
		if( (freqs[x].numFlags>0) && (freqs[x].numWeights>0) )
		{
			freqs[x].percentFlags = ((double)freqs[x].numFlags) / ((float)ta->numFlags) * 100.0;
			freqs[x].percentWeights = ((double)freqs[x].numWeights) / ((float)ta->numWeights) * 100.0;
		}
		else
		{
			freqs[x].percentFlags = 0;
			freqs[x].percentWeights = 0;
		}
	}

	/* at this point # flags, %flags, # weights, % weights, and # of players for every freq
	 * and # flags, # weights, and # players for arena are recorded */

	// now that we know how many players there are, check if there are enough players for rewards
	if( ta->numPlayers < ta->min_players_in_arena )
	{
		logman->Log(L_DRIVEL,
			"<turf_reward> {%s} Not enough players in arena for rewards.  Current:%i Minimum:%i\n",
			arenaman->arenas[arena].name,
			ta->numPlayers,
			ta->min_players_in_arena);
		chat->SendArenaSoundMessage(arena, SOUND_BEEP1, "Notice: not enough players for rewards.");
		return;
	}

	// count how many valid teams exist (valid meaning enough players to be considered a team)
	for(x=0 ; x<MAXFREQ ; x++)
		if( freqs[x].numPlayers > ta->min_players_on_freq )
			ta->numTeams++;

	/* at this point # flags, %flags, # weights, % weights, and # of players for every freq
	 * and # flags, # weights, # players, and # teams for arena are recorded */

	// now that we know how many teams there are, check if there are enough teams for rewards
	if(ta->numTeams < ta->min_teams)
	{
		logman->Log(L_DRIVEL, "<turf_reward> {%s} Not enough teams in arena for rewards.  Current:%i Minimum:%i\n",
			arenaman->arenas[arena].name,
			ta->numTeams,
			ta->min_teams);
		chat->SendArenaSoundMessage(arena, SOUND_BEEP1, "Notice: not enough teams for rewards.");
		return;
	}

	// figure out which teams pass minimum requirements for getting rewards
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

	// fill in percapita for all teams, but *** ONLY ADD IT TO THE SUM IF THEY PASSED REQUIREMENTS ***
	for(x=0 ; x<MAXFREQ ; x++)
	{
		if(freqs[x].numPlayers>0)	// double check, min_players_on_freq should have already weeded any out
		{
			freqs[x].perCapita = ((double)freqs[x].numWeights) / ((float)freqs[x].numPlayers);

			// only add to sum if freq passed minimum requirements
			if(score[x])
				ta->sumPerCapitas+=freqs[x].perCapita;
		}
	}

	/* at this point # flags, %flags, # weights, % weights, and # of players for every freq
	 * and # flags, # weights, # players, # teams, and sumPerCapita for arena are recorded */

	// figure out percent of jackpot team will recieve and how many points that relates to
	ta->numPoints = ta->jackpot_modifier * ta->numPlayers;

	if(ta->sumPerCapitas<1)
	{
		// this should not happen but, just to be safe
		// no one gets points
		for(x=0 ; x<MAXFREQ ; x++)
		{
			freqs[x].percent = 0;
			freqs[x].numPoints  = 0;
		}

		logman->Log(L_WARN, "<turf_reward> {%s} sumPerCapitas<1. Check that MinFlags, MinWeights, and be sure all weights are > 0.",
			arenaman->arenas[arena].name);
		chat->SendArenaSoundMessage(arena, SOUND_BEEP1, "Notice: not enough teams for rewards.");

		return;
	}

	for(x=0 ; x<MAXFREQ ; x++)
	{
		if(score[x])	// only for teams that passed requirements
		{
			freqs[x].percent = (double)(freqs[x].perCapita / ta->sumPerCapitas * 100.0);
			if(freqs[x].numPlayers>0)	// double check, min_players_on_freq should have already weeded any out
			{
				freqs[x].numPoints = (int)(ta->numPoints * (freqs[x].percent/100) / freqs[x].numPlayers);
			}
			else
			{
				// this should never ever happen
				logman->Log(L_WARN,
					"<turf_reward> {%s} When calculating numPoints, a team had 0 players. Check that min_players_freq > 0.",
					arenaman->arenas[arena].name);
			}
		}
		else
		{
			// make sure teams that didn't pass requirements get nothing
			freqs[x].percent = 0;
			freqs[x].numPoints  = 0;
		}
	}
}

local void crPeriodic(int arena)
{
	int x;
	int modifier = tr[arena].jackpot_modifier;

	crStandard(arena);	// cheap way of doing it
				// TODO: change to do it's own dirty work

	if(modifier > 0)
	{
		for(x=0 ; x<MAXFREQ ; x++)
		{
			tr[arena].freqs[x].percent = 0;
			tr[arena].freqs[x].numPoints = modifier * tr[arena].freqs[x].numFlags;
		}
	}
	else
	{
		int numPlayers = tr[arena].numPlayers;

		for(x=0 ; x<MAXFREQ ; x++)
		{
			tr[arena].freqs[x].percent = 0;
			tr[arena].freqs[x].numPoints = numPlayers * (-modifier) * tr[arena].freqs[x].numFlags;
		}
	}
}

local void awardPts(int arena)
{
	int x;
	struct TurfArena *ta	= &tr[arena];
	struct FreqInfo *freqs = ta->freqs;

	// this is where we award each player that deserves points
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
			else if(freqs[playerdata->players[x].freq].numPoints)
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
}

local void updateFlags(int arena)
{
	int x;
	struct TurfArena *ta = &tr[arena];
	struct TurfFlag *flags = ta->flags;

	// increment numdings and weights for all owned flags
	for(x=0 ; x<ta->numFlags ; x++)
	{
		struct TurfFlag *flagPtr = &flags[x];
		struct OldNode *oPtr;
		Link *l, *next;

		if(flagPtr->freq!=-1)
		{
			// flag is owned, increment # dings and update weight accordingly
			flagPtr->dings++;
			flagPtr->weight=calculateWeight(flagPtr->dings);
		}

		// update lastOwned for all entries on linked list

		// increment lastOwned for every node
		// check all nodes after head
		for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
		{
			next = l->next;
			oPtr = l->data;

			// increment lastOwned before check
			if(++oPtr->lastOwned > tr[arena].recover_dings)
			{
				// entry for team that lost chance to recover flag
				LLRemove(&flagPtr->old, oPtr);
				afree(oPtr);
			}
		}
	}
}

local helptext_t turfresetflags_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the turf reward flag data.\n";

local void C_turfResetFlags(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	flagGameReset(arena);
}

local helptext_t forceding_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Forces a reward to take place immediately in your current arena.\n";

void C_forceDing(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	turfRewardTimer(&arena);
}

