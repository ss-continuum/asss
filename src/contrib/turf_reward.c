/* ----------------------------------------------------------------------------------------------------
 *
 *      Turf Reward Module for ASSS - a very specialized version of the Turf Zone reward system
 *                             by GiGaKiLLeR <gigamon@hotmail.com>
 *
 * ----------------------------------------------------------------------------------------------------
 *
 * Reward System Specs:
 *
 * - Weighted flag system
 *      1. Each flag has a weight, the longer a flag is held the more it is "worth"
 *      2. When a previously owned flag is tagged back before a set number of dings (set in cfg),
 *         the flag's full "worth"/weight is restored
 *
 *         Example trying to show SOME of the possibilities:
 *             My team owns a flag worth 4 WU (weight units) upon next ding.  The enemy tags
 *             that flag, now worth 1 WU upon next ding.
 *             Case 1: Ding.  The enemy team recieves the 1 WU for that flag since they own it.
 *             Case 2: I tag the flag back to my team.  Flag is worth 4 WU as before (NOT 1 WU).
 *                     Ding.  I am rewarded with 4 WU for that flag, and it is worth 5 WU upon
 *                     next ding.
 *             Case 3: ** Depending on settings in cfg **
 *                     Ding.  The enemy team recieves the 1 WU for that flag since they own it.
 *                     The flag is now worth 2 WU for the enemy team upon next ding.
 *                     I tag the flag back to my team.  Flag is worth 4 WU as before (NOT 1 WU).
 *                     Ding. I am rewarded 4 WU for that flag, and it will be worth 5 WU upon
 *                     next ding.  However, note that the enemy team has their chance to recover
 *                     the flag for 2 WU.
 *             The module is not limited to 2 teams having previous ownership of a flag (as in
 *             this example).  There can be an arbitrary number of teams that have had previous
 *             ownership and the ability to 'recover' the flag for their respective previous worth.
 *
 * - Scoring is not based on # of flags your team owns but rather the # of weights to those flags.
 *      The algorithm (REWARD_STD) is:
 *
 *      PerCapita = # of weights per person on team = (# of weights / # ppl on the team)
 *
 *      Sum all team's PerCapita's.
 *
 *      % of Jackpot to recieve = (PerCapita / Sum of PerCapita's) * 100
 *
 *      Jackpot = # players * 200    (NOTE: linear jackpot may not be optimal)
 *                                   TODO: add ability to set how jackpot is calculated from cfg
 *
 *      Points to be awarded = ( Jackpot * (% of jackpot to recieve/100) ) / (# ppl on team)
 *
 * - Goals of this scoring method
 *      1.  The more people on a team.  The less points awarded.  All other variables held constant.
 *              There is no more incentive to fill your team up to the max, but rather figure out
 *              the optimal # of players (skillwise/teamwise) to get the maximium benefit/points
 *              with respect to the amount of opposition (other teams).
 *      2.  The longer flags are held the more they are worth. (incentive to "Hold your Turf!")
 *              Territory you already own will be worth more to keep, rather than losing it while
 *              taking over a new area.
 *      3.  Lost territory can be regained at full worth if reclaimed before a set number of dings.
 *              Additional strategy as certain areas on the map can be worth more to reclaim/attack,
 *              leaving the door wide open to the player's decision on every situation.
 *
 * ----------------------------------------------------------------------------------------------------
 */

#include "asss.h"                       // necessary include to connect the module
#include "turf_reward.h"
#include "settings/turfreward.h"        // bring in the settings for reward types

// default values for cfg settings
#define MIN_PLAYERS_ON_FREQ     3       // min # of players needed on a freq for that freq to recieve reward pts
#define MIN_PLAYERS_IN_ARENA    6       // min # of players needed in the arena for anyone to recieve reward pts
#define MIN_TEAMS               2       // min # of teams needed for anyone to recieve reward pts
#define MIN_FLAGS               1       // min # of flags needed to be owned in order to recieve reward pts
#define MIN_PERCENT_FLAGS       0       // min % of flags needed to be owned by freq in order to recieve reward pts
#define MIN_WEIGHTS             1       // min # of weights needed for a team to recieve reward pts
#define MIN_PERCENT_WEIGHTS     0       // min % of weights needed to be owned by freq in order to recieve reward pts
#define MIN_PERCENT             0       // min % of weights needed to recieve an award
#define JACKPOT_MODIFIER        200     // modifies the jackpot based on how many points per player playing
                                        // jackpot = # players playing * pointModifier (subject to change)
#define TIMER_INITIAL           6000    // starting timer
#define TIMER_INTERVAL          6000    // subsequent timer intervals
#define RECOVER_DINGS           0       // number of dings a flag will be "recoverable"
//#define MAX_POINTS            5000    // maximum # of points a player can be awarded
//#define WIN_PERCENT           100     // percent of weights needed for a flag game victory
//#define WIN_RESET             0       // reset flags on flag game victory?

// easy calls for mutex
#define LOCK_STATUS(arena) \
        pthread_mutex_lock(trmtx + arena)
#define UNLOCK_STATUS(arena) \
        pthread_mutex_unlock(trmtx + arena)


// interfaces to various other modules I will probably have to use
local Imodman     *mm;                  // to get interfaces
local Iplayerdata *playerdata;          // player data
local Iarenaman   *arenaman;            // arena manager, to get conf handle for arena
local Iflags      *flagsman;            // to access flag tags and info
local Iconfig     *config;              // conf (for arena .conf) services
local Istats      *stats;               // stat / score services
local Ilogman     *logman;              // logging services
local Imainloop   *mainloop;            // main loop - for setting the ding timer
local Imapdata    *mapdata;             // get to number of flags in each arena
local Ichat       *chat;                // message players
local Icmdman     *cmdman;              // for command handling
local Inet        *net;                 // to send packet for score update


/* global (to this file) declarations */
local struct TurfArena *tr;             // huge array of turf reward data for every arena
local pthread_mutex_t trmtx[MAXARENA];  // to keep things thread safe when accessing tr


/* function prototypes */
// connected to callbacks
local void arenaAction(int arena, int action);                 // arena creation and destruction, or conf changed
local void flagTag(int arena, int pid, int fid, int oldfreq);  // does everything necessary when a flag is claimed
local int turfRewardTimer(void *arenaPtr);                     // called when a reward is due to be processed

// connected to interface
local void flagGameReset(int arena);    // reset all flag data (also resets it in flags module)
local void dingTimerReset(int arena);   // reset the timer to an arena
local void doReward(int arena);         // to force a reward to happen immediately
local void LockTurfStatus(int arena);
local void UnlockTurfStatus(int arena);

// helper / utility functions
local void loadSettings(int arena);             // reads the settings for an arena from cfg
local void clearArenaData(int arena);           // clears out an arena's data (not including freq, flag, or numFlags)
local void clearFreqData(int arena);            // clears out the freq data for a particular arena
local void clearFlagData(int arena, int init);  // clears out the flag data for a particular arena
local int calculateWeight(int numDings);        // figure out how much a flag is worth based on it's # dings
//local int calcWeight(int arena, int numDings);  // figure out how much a flag is worth based on it's # dings
local void preCalc(int arena, struct TurfArena *ta);  // does a few calculations that will make writing external calculation modules a lot easier
local void awardPts(int arena);                 // award pts to each player based on previously done calculations
local void updateFlags(int arena);              // increment the numDings for all owned flags and recalculate their weights
local struct FreqInfo * getFreqPtr(int arena, int freq); // get a pointer to a freq
                                                         // (if it doesn't exist, creates the freq, and returns a pointer to it
                                                         // if freq exists, just returns pointer to existing one)
local void cleanup(void *arenaPtr);  // releases the scoring interface when arena is destroyed
local void scorePkt(LinkedList freqs, byte *pkt);

/* functions for commands */
// standard user commands
local void C_turfTime(const char *, int, const Target *);       // to find out how much time till next ding
local void C_turfInfo(const char *, int, const Target *);       // to get settings info on minimum requirements, etc

// mod commands
local void C_turfResetFlags(const char *, int, const Target *); // to reset the flag data on all flags
local void C_forceDing(const char *, int, const Target *);      // to force a ding to occur, does not change the timer
local void C_turfResetTimer(const char *, int, const Target *); // to reset the timer

local helptext_t turftime_help, turfinfo_help, turfresetflags_help, turfresettimer_help, forceding_help;


/* connect interface */
local Iturfreward _myint =
{
	INTERFACE_HEAD_INIT(I_TURFREWARD, "turfreward-core")
	flagGameReset, dingTimerReset, doReward,
	LockTurfStatus, UnlockTurfStatus
};


EXPORT const char info_turf_reward[] = "v3.4 by GiGaKiLLeR <gigamon@hotmail.com>";


/* the actual entrypoint into this module */
EXPORT int MM_turf_reward(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)  // when the module is to be loaded
	{
		mm = _mm;

		// get all of the interfaces that we are to use
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		arenaman   = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		flagsman   = mm->GetInterface(I_FLAGS,      ALLARENAS);
		config     = mm->GetInterface(I_CONFIG,     ALLARENAS);
		stats      = mm->GetInterface(I_STATS,      ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		mainloop   = mm->GetInterface(I_MAINLOOP,   ALLARENAS);
		mapdata    = mm->GetInterface(I_MAPDATA,    ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		cmdman     = mm->GetInterface(I_CMDMAN,     ALLARENAS);
		net        = mm->GetInterface(I_NET,        ALLARENAS);

		// if any of the interfaces are null then loading failed
		if (!playerdata || !arenaman || !flagsman || !config || !stats || !logman || !mainloop || !mapdata || !chat || !cmdman || !net)
			return MM_FAIL;

		// special turf_reward commands
		cmdman->AddCommand("turftime", C_turfTime, turftime_help);
		cmdman->AddCommand("turfinfo", C_turfInfo, turfinfo_help);
		cmdman->AddCommand("forceding", C_forceDing, forceding_help);
		cmdman->AddCommand("turfresetflags", C_turfResetFlags, turfresetflags_help);
		cmdman->AddCommand("turfresettimer", C_turfResetTimer, turfresettimer_help);

		// initialize the tr array
		{
			int x;
			tr = amalloc(MAXARENA * sizeof(struct TurfArena));

			for(x=0 ; x<MAXARENA ; x++)
			{
				tr[x].reward_style         = 0;
				tr[x].min_players_on_freq  = 0;
				tr[x].min_players_in_arena = 0;
				tr[x].min_teams            = 0;
				tr[x].min_flags            = 0;
				tr[x].min_percent_flags    = 0;
				tr[x].min_weights          = 0;
				tr[x].min_percent_weights  = 0;
				tr[x].min_percent          = 0;
				tr[x].jackpot_modifier     = 0;
				tr[x].recover_dings        = 0;
				tr[x].set_weights          = 0;
				tr[x].weights              = NULL;

				tr[x].arena          = -1;
				tr[x].dingTime       = 0;
				tr[x].timer_initial  = 0;
				tr[x].timer_interval = 0;
				tr[x].trp            = NULL;

				tr[x].numFlags = 0;
				clearArenaData(x);
				tr[x].flags = NULL;
			}
		}

		// initialize the mutual exclusions
		{
			int x;
			pthread_mutexattr_t attr;
			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

			for (x=0; x < MAXARENA; x++)
				pthread_mutex_init(trmtx + x, &attr);
			pthread_mutexattr_destroy(&attr);
		}

		// register the interface for turf_reward
		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)  // when the module is to be unloaded
	{
		int x;

		// unregister the interface for turf_reward
		if(mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		// make sure all timers are gone
		mainloop->CleanupTimer(turfRewardTimer, -1, cleanup);

		// get rid of turf_reward commands
		cmdman->RemoveCommand("turftime", C_turfTime);
		cmdman->RemoveCommand("turfinfo", C_turfInfo);
		cmdman->RemoveCommand("forceding", C_forceDing);
		cmdman->RemoveCommand("turfresetflags", C_turfResetFlags);
		cmdman->RemoveCommand("turfresettimer", C_turfResetTimer);

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
		mm->ReleaseInterface(net);

		// double check that all arena's sucessfully called AA_DESTROY
		for(x=0 ; x<MAXARENA ; x++)
		{
			// if there is existing weights data, discard
			if (tr[x].weights)
			{
				afree(tr[x].weights);
				tr[x].weights = NULL;
			}

			// if there is existing flags data, discard
			if (tr[x].flags)
			{
				clearFlagData(x, 0);
				afree(tr[x].flags);
				tr[x].flags = NULL;
			}

			// if there is existing freqs data, discard
			clearFreqData(x);
		}

		// clear the massive array
		afree(tr);

		return MM_OK;
	}
	else if (action == MM_ATTACH)  // module only attached to an arena if listed in conf
	{
		// create all necessary callbacks
		mm->RegCallback(CB_ARENAACTION, arenaAction, arena); // for arena create & destroy, config changed
		mm->RegCallback(CB_FLAGPICKUP, flagTag, arena);      // for when a flag is tagged
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		// unregister all the callbacks
		mm->UnregCallback(CB_ARENAACTION, arenaAction, arena);
		mm->UnregCallback(CB_FLAGPICKUP, flagTag, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


local void arenaAction(int arena, int action)
{
	LOCK_STATUS(arena);

	if (action == AA_CREATE)
	{
		loadSettings(arena);

		tr[arena].numFlags = mapdata->GetFlagCount(arena);
		clearArenaData(arena);

		// create and initialize all the flags
		tr[arena].flags = amalloc(tr[arena].numFlags * sizeof(struct TurfFlag));
		clearFlagData(arena, 1);

		// create and intialize the data on freqs
		LLInit(&tr[arena].freqs);

		tr[arena].trp = mm->GetInterface(I_TURFREWARD_POINTS, arena);

		// set up the timer for arena
		if(tr[arena].multi_arena_id != -1)
		{
			// multi arena enabled, use the id as the key for the timer
			tr[arena].arena = arena;
			tr[arena].dingTime = GTC();
			mainloop->SetTimer(turfRewardTimer, tr[arena].timer_initial, tr[arena].timer_interval, &tr[arena].arena, tr[arena].multi_arena_id);
		}
		else
		{
			// single arena only
			tr[arena].arena = arena;
			tr[arena].dingTime = GTC();
			mainloop->SetTimer(turfRewardTimer, tr[arena].timer_initial, tr[arena].timer_interval, &tr[arena].arena, arena);
		}
	}
	else if (action == AA_DESTROY)
	{
		// clear old timer and cleanup the I_TURFREWARD_POINTS interface
		mainloop->CleanupTimer(turfRewardTimer, arena, cleanup);

		// clean up any old arena data
		clearArenaData(arena);

		// if there is existing weights data, discard
		if (tr[arena].weights)
		{
			afree(tr[arena].weights);
			tr[arena].weights = NULL;
		}

		// if there is existing flags data, discard
		if (tr[arena].flags)
		{
			clearFlagData(arena, 0);
			afree(tr[arena].flags);
			tr[arena].flags = NULL;
		}

		// if there is existing freqs data, discard
		clearFreqData(arena);
	}
	else if (action == AA_CONFCHANGED)
	{
		int initial  = tr[arena].timer_initial;
		int interval = tr[arena].timer_interval;
		loadSettings(arena);
		if( (initial!=tr[arena].timer_initial) || (interval!=tr[arena].timer_interval) )
		{
			chat->SendArenaMessage(arena, "Ding timer settings in config was changed. Updating...");
			dingTimerReset(arena);
		}
	}

	UNLOCK_STATUS(arena);
}


local void loadSettings(int arena)
{
	ConfigHandle c = arenaman->arenas[arena].cfg;

	tr[arena].reward_style         = config->GetInt(c, "TurfReward", "RewardStyle", REWARD_STD);
	tr[arena].multi_arena_id       = config->GetInt(c, "TurfReward", "MultiArenaID", -1);
	tr[arena].min_players_on_freq  = config->GetInt(c, "TurfReward", "MinPlayersFreq", MIN_PLAYERS_ON_FREQ);
	tr[arena].min_players_in_arena = config->GetInt(c, "TurfReward", "MinPlayersArena", MIN_PLAYERS_IN_ARENA);
	tr[arena].min_teams            = config->GetInt(c, "TurfReward", "MinTeams", MIN_TEAMS);
	tr[arena].min_flags            = config->GetInt(c, "TurfReward", "MinFlags", MIN_FLAGS);
	tr[arena].min_percent_flags    = (double)config->GetInt(c, "TurfReward", "MinFlagsPercent", MIN_PERCENT_FLAGS) / 1000.0;
	tr[arena].min_weights          = config->GetInt(c, "TurfReward", "MinWeights", MIN_WEIGHTS);
	tr[arena].min_percent_weights  = (double)config->GetInt(c, "TurfReward", "MinWeightsPercent", MIN_PERCENT_WEIGHTS) / 1000.0;
	tr[arena].min_percent          = (double)config->GetInt(c, "TurfReward", "MinPercent", MIN_PERCENT) / 1000.0;
	tr[arena].jackpot_modifier     = config->GetInt(c, "TurfReward", "JackpotModifier", JACKPOT_MODIFIER);
	tr[arena].timer_initial        = config->GetInt(c, "TurfReward", "TimerInitial", TIMER_INITIAL);
	tr[arena].timer_interval       = config->GetInt(c, "TurfReward", "TimerInterval", TIMER_INTERVAL);
	tr[arena].recover_dings        = config->GetInt(c, "TurfReward", "RecoverDings", RECOVER_DINGS);

	// if there is existing weights data, discard
	if (tr[arena].weights)
	{
		afree(tr[arena].weights);
		tr[arena].weights = NULL;
	}
	tr[arena].set_weights          = config->GetInt(c, "TurfReward", "SetWeights", 0);

	if(tr[arena].set_weights<1)
	{
		// user didn't set the weights, just set 1 weight of 1 WU then
		tr[arena].set_weights = 1;
		tr[arena].weights = amalloc(sizeof(int));
		tr[arena].weights[0]=1;
	}
	else
	{
		//int x;
		//int defaultVal = 1;  // to keep it non-decreasing and non-increasing if cfg didn't specify
		tr[arena].weights = amalloc(tr[arena].set_weights * sizeof(int));

		/* FIXME: not sure how to do string manipulation stuff in C
		for(x=0 ; x<tr[arena].set_weights ; x++)
		{
			// not sure how to do this in C
			// wStr = "Weight" + x
			char *wStr;
			tr[arena].weights[x] = config->GetInt(c, "TurfReward", wStr, defaultVal);
			defaultVal = tr[arena].weights[x];
		}
		*/
	}

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
	if(tr[arena].timer_interval < 1500)     // 15 second safety so that server isn't overloaded by dings
		tr[arena].timer_interval = 1500;
}


local void clearArenaData(int arena)
{
	tr[arena].numPlayers    = 0;
	tr[arena].numTeams      = 0;
	tr[arena].numWeights    = 0;
	tr[arena].numPoints     = 0;
	tr[arena].sumPerCapitas = 0;
	tr[arena].numTags       = 0;
	tr[arena].numLost       = 0;
	tr[arena].numRecovers   = 0;
}


local void clearFreqData(int arena)
{
	LLEnum(&tr[arena].freqs, afree);
	LLEmpty(&tr[arena].freqs);
}


local void clearFlagData(int arena, int init)
{
	int x;
	for(x=0 ; x<tr[arena].numFlags ; x++)
	{
		struct TurfFlag *ptr = &tr[arena].flags[x];

		ptr->dings     = -1;
		ptr->freq      = -1;
		ptr->weight    =  0;
		ptr->taggerPID = -1;

		// now clear out the linked list 'old'
		if (init)
			LLInit(&ptr->old);
		LLEnum(&ptr->old, afree);
		LLEmpty(&ptr->old);
	}
}


local void flagTag(int arena, int pid, int fid, int oldfreq)
{
	int freq=-1;
	int r_freq=-1, r_dings, r_weight, r_pid, // flag recover data
	    l_freq=-1, l_dings, l_weight, l_pid; // flag lost data
	struct TurfFlag *pTF   = NULL;  // pointer to turf flag
	struct OldNode  *oPtr  = NULL;  // pointer to node of linked list holding previous owners
	struct FreqInfo *pFreq = NULL;  // pointer to freq that tagged flag

	Link *l = NULL;  // pointer to a node in the linked list

	if (ARENA_BAD(arena))
	{
		logman->LogP(L_MALICIOUS, "turf_reward", pid, "Flag was tagged for bad/nonexistent arena");
		return;
	}

	if(fid < 0 || fid >= tr[arena].numFlags)
	{
		logman->LogP(L_MALICIOUS, "turf_reward", pid,
			"nonexistent flag tagged: %d not in 0..%d",
			fid, tr[arena].numFlags-1);
		return;
	}

	LOCK_STATUS(arena);

	freq = playerdata->players[pid].freq;
	pTF = &tr[arena].flags[fid];

	if(pTF->freq==freq)
	{
		// flag was already owned by that team
		UNLOCK_STATUS(arena);
		logman->LogP(L_MALICIOUS, "turf_reward", pid, "Flag tagged was already owned by player's team.");
		return;
	}

	// increment number of flag tags
	pFreq = getFreqPtr(arena, freq);
	pFreq->numTags++;
	tr[arena].numTags++;

	if(pTF->freq>-1)
	{
		// flag that was tagged was owned by another team
		struct FreqInfo *pFreqOld;

		oPtr = amalloc(sizeof(struct OldNode));
		oPtr->lastOwned = 0;
		oPtr->freq      = l_freq   = pTF->freq;
		oPtr->dings     = l_dings  = pTF->dings;
		oPtr->weight    = l_weight = pTF->weight;
		oPtr->taggerPID = l_pid    = pTF->taggerPID;

		// flag was lost, that freq now gets a chance for recovery (add into linked list)
		// since this team owned the flag, there is no way it can already be listed in linked list
		// so simply add it in
		LLAddFirst(&pTF->old, oPtr);

		// increment number of flag losses
		pFreqOld = getFreqPtr(arena, l_freq);
		pFreqOld->numLost++;
		tr[arena].numLost++;
	}

	// search for matching freq in linked list of teams that have chance to recover
	for(l = LLGetHead(&pTF->old); l; l = l->next)
	{
		oPtr = l->data;

		if(oPtr->freq == freq)
		{
			// found entry that matches freq, meaning flag was recovered
			r_freq   = pTF->freq      = freq;
			r_dings  = pTF->dings     = oPtr->dings;
			r_weight = pTF->weight    = oPtr->weight;
			r_pid    = pTF->taggerPID = pid;  // pid of player who recovered flag now gets taggerPID

			// remove node from linked list
			LLRemove(&pTF->old, oPtr);

			// increment number of flag recoveries
			pFreq->numRecovers++;
			tr[arena].numRecovers++;

			break;  // end the for loop
		}
	}

	if(r_freq==-1)
	{
		// flag wasn't recovered, fill in data for newly tagged flag
		pTF->freq      = freq;
		pTF->dings     = 0;
		pTF->weight    = calculateWeight(pTF->dings);
		//pTF->weight    = calcWeight(arena, pTF->dings);
		pTF->taggerPID = pid;
	}

	UNLOCK_STATUS(arena);

	logman->LogP(L_DRIVEL, "turf_reward", pid, "Flag was tagged");

	// finally do whatever callbacks are necessary
	DO_CBS(CB_TURFTAG, arena, TurfTagFunc, (arena, pid, fid));
	if(r_freq!=-1)
		DO_CBS(CB_TURFRECOVER, arena, TurfRecoverFunc, (arena, fid, r_pid, r_freq, r_dings, r_weight));
	if(l_freq!=-1)
		DO_CBS(CB_TURFLOST, arena, TurfLostFunc, (arena, fid, l_pid, l_freq, l_dings, l_weight));
}


/*local int calcWeight(int arena, int numDings)
{
	if (numDings < 0) return 0;
	if (numDings > tr[arena].set_weights-1)
		return tr[arena].weight[tr[arena].set_weights-1];
	return tr[arena].weight[numDings];
}*/


// FIXME: get rid of this function when loadSettings is fixed and changeover to calcWeight for all calls
local int calculateWeight(int numDings)
{
	switch(numDings)
	{
	case -1: return 0;
	case  0: return 100;
	case  1: return 141;
	case  2: return 173;
	case  3: return 200;
	case  4: return 223;
	case  5: return 244;
	case  6: return 264;
	case  7: return 282;
	case  8: return 300;
	case  9: return 316;
	case 10: return 331;
	case 11: return 346;
	default: break;
	}
	return numDings>11 ? 400 : 0;
}


local int turfRewardTimer(void *arenaPtr)
{
	int *aPtr = arenaPtr;
	int arena = *aPtr;

	if (ARENA_BAD(arena)) return 0;  // if timer was called for bad arena, get rid of timer

	// we have a go for starting the reward sequence
	doReward(arena);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "Timer Ding");

	return 1;  // yes we want timer called again
}


local void doReward(int arena)
{
	trstate_t action;

	if (ARENA_BAD(arena)) return;

	LOCK_STATUS(arena);

	if (!tr || !tr[arena].flags)
	{
		UNLOCK_STATUS(arena);
		return;
	}

	/* do pre-calculations (to make it easier for ppl to write a score calculation interface)
	 * This ensures that all teams that own a flag (doesn't have to have players) are in the
	 * linked list tr[x].freqs and also that all teams with people on them are in the linked
	 * list.  Therefore, all freqs that need scoring are already in the linked list before
	 * calling the scoring interface. */
	preCalc(arena, &tr[arena]);

	// calculate the points to award using interface for score calculations
	if (tr[arena].trp)
		action = tr[arena].trp->CalcReward(arena,tr);
	else
		action = TR_FAIL_CALCULATIONS;

	// check if we are to award points
	if (action==TR_AWARD_UPDATE || action==TR_AWARD_ONLY)
	{
		awardPts(arena);

		// send points update packet to everyone in arena (adopted from periodic.c)
		{
			int freqcount=0;
			struct FreqInfo *pFreq;
			Link *l;
			byte *pkt;

			// count the number of teams that recieved points
			for(l = LLGetHead(&tr[arena].freqs) ; l ; l=l->next)
			{
				pFreq  = l->data;

				if(pFreq->numPoints > 0)
					freqcount++;
			}

			pkt = amalloc(freqcount*4+1);
			pkt[0] = S2C_PERIODICREWARD;

			// fill in packet data and send it to the arena
			scorePkt(tr[arena].freqs, pkt + 1);
			net->SendToArena(arena, -1, pkt, freqcount*4+1, NET_RELIABLE);

			afree(pkt);
			pkt = NULL; /* just to be safe */
		}
	}

	// do the callback for post-reward (WHILE LOCK IS IN PLACE)
	DO_CBS(CB_TURFPOSTREWARD, arena, TurfPostRewardFunc, (arena, &tr[arena]));

	// check if we are to update flags
	if (action==TR_AWARD_UPDATE || action==TR_UPDATE_ONLY || action==TR_FAIL_REQUIREMENTS)
		updateFlags(arena);

	// new freq data for next round
	clearFreqData(arena);           // intialize the data on freqs

	UNLOCK_STATUS(arena);
}


local void scorePkt(LinkedList freqs, byte *pkt)
{
	Link *l;
	struct FreqInfo *pFreq;

	for(l = LLGetHead(&freqs) ; l ; l=l->next)
	{
		int freq, points;

		pFreq  = l->data;
		freq   = pFreq->freq;
		points = pFreq->numPoints;

		if(points > 0)
		{
			/* enter in packet (from periodic.c) */
			*(pkt++) = (freq>>0) & 0xff;
			*(pkt++) = (freq>>8) & 0xff;
			*(pkt++) = (points>>0) & 0xff;
			*(pkt++) = (points>>8) & 0xff;
		}
	}
}


local void preCalc(int arena, struct TurfArena *ta)
{
	int x;
	struct FreqInfo *pFreq;

	// go through all flags and if owned, updating numFlags and numWeight for the corresponding freq
	for(x=0 ; x<ta->numFlags ; x++)
	{
		int freq, dings, weight;
		struct TurfFlag *flagPtr = &ta->flags[x];

		freq   = flagPtr->freq;
		dings  = flagPtr->dings;
		weight = flagPtr->weight;

		if (freq>=0 && dings>=0 && weight>=0)
		{
			// flag is owned
			pFreq = getFreqPtr(arena, freq);  // get pointer to freq
			pFreq->numFlags++;
			pFreq->numWeights+=weight;
			ta->numWeights+=weight;
		}
	}

	// go through all players and update freq info on numPlayers
	playerdata->LockStatus();
	for(x=0 ; x<MAXPLAYERS ; x++)
	{
		struct PlayerData *pdPtr = &playerdata->players[x];
		if ( (pdPtr->arena==arena) && (pdPtr->shiptype!=SPEC) && (pdPtr->status==S_PLAYING) )
		{
			pFreq = getFreqPtr(arena, pdPtr->freq);
			pFreq->numPlayers++;
			ta->numPlayers++;
		}
	}
	playerdata->UnlockStatus();
}


local void awardPts(int arena)
{
	int x;

	// this is where we award each player that deserves points
	playerdata->LockStatus();
	for(x=0 ; x<MAXPLAYERS ; x++)
	{
		if (playerdata->players[x].arena == arena)
		{
			unsigned int points;
			struct FreqInfo *pFreq;

			// player is in arena
			if ((playerdata->players[x].shiptype == SPEC) || (playerdata->players[x].status!=S_PLAYING))
			{
				// player is in spec/not playing
				chat->SendSoundMessage(x, SOUND_DING, "Reward: 0 (not playing)");
			}
			else if ( (points = (pFreq = getFreqPtr(arena, playerdata->players[x].freq))->numPoints) )
			{
				// player is on a freq that recieved points
				stats->IncrementStat(x, STAT_FLAG_POINTS, points);            // award player
				chat->SendSoundMessage(x, SOUND_DING, "Reward: %i", points);  // notify player
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


local void updateFlags(int arena)
{
	int x;
	struct TurfArena *ta   = &tr[arena];
	struct TurfFlag *flags = ta->flags;

	// increment numdings and weights for all owned flags
	for(x=0 ; x<ta->numFlags ; x++)
	{
		struct TurfFlag *flagPtr = &flags[x];
		struct OldNode *oPtr;
		Link *l, *next;

		if (flagPtr->freq!=-1)
		{
			// flag is owned, increment # dings and update weight accordingly
			flagPtr->dings++;
			flagPtr->weight=calculateWeight(flagPtr->dings);
			//flagPtr->weight=calcWeight(arena, flagPtr->dings);
		}

		// increment lastOwned for every node
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


local helptext_t turftime_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the amount of time till next ding.\n";
local void C_turfTime(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	unsigned int time;
	if (ARENA_BAD(arena)) return;

	LOCK_STATUS(arena);
	time = GTC() - tr[arena].dingTime / 100;
	UNLOCK_STATUS(arena);

	if(time!=0)
		chat->SendMessage(pid, "Next ding in: %d seconds.", time);
}


local helptext_t turfinfo_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the current settings / requirements to recieve awards.\n";
local void C_turfInfo(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	if (ARENA_BAD(arena)) return;

	LOCK_STATUS(arena);
	chat->SendMessage(pid, "+---- Arena Requirements ----+");
	chat->SendMessage(pid, "   Minimum players: %d", tr[arena].min_players_in_arena);
	chat->SendMessage(pid, "     Minimum teams: %d", tr[arena].min_teams);
	chat->SendMessage(pid, "+---- Team Requirements  ----+");
	chat->SendMessage(pid, "   Minimum players: %d", tr[arena].min_players_on_freq);
	chat->SendMessage(pid, "   Minimum # flags: %d", tr[arena].min_flags);
	chat->SendMessage(pid, "   Minimum %% flags: %d", tr[arena].min_percent_flags);
	chat->SendMessage(pid, " Minimum # weights: %d", tr[arena].min_weights);
	chat->SendMessage(pid, " Minimum %% weights: %d", tr[arena].min_percent_weights);
	chat->SendMessage(pid, "  Jackpot modifier: %d", tr[arena].jackpot_modifier);
	chat->SendMessage(pid, "+---- Misc. Useful Info  ----+");
	chat->SendMessage(pid, "        Ding every: %d seconds", tr[arena].timer_interval/100);
	chat->SendMessage(pid, "   Recovery cutoff: %d dings", tr[arena].recover_dings);
	UNLOCK_STATUS(arena);
}


local helptext_t turfresetflags_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the turf_reward module's and flags module's flag data in your current arena.\n";
local void C_turfResetFlags(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	if (ARENA_BAD(arena)) return;
	flagGameReset(arena);  // does the locking for us already
}


local helptext_t forceding_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Forces a reward to take place immediately in your current arena.\n";
local void C_forceDing(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	if (ARENA_BAD(arena)) return;
	doReward(arena);  // does the locking for us already
}


local helptext_t turfresettimer_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the ding timer in your current arena.\n";
local void C_turfResetTimer(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	if (ARENA_BAD(arena)) return;
	dingTimerReset(arena);  // does the locking for us already
}


local void flagGameReset(int arena)
{
	int x;

	if (ARENA_BAD(arena)) return;

	LOCK_STATUS(arena);

	// clear the data in this module
	if (tr && tr[arena].flags)
		clearFlagData(arena, 0);

	// clear the data in the flags module
	flagsman->LockFlagStatus(arena);
	for(x=0 ; x<flagsman->flagdata[arena].flagcount ; x++)
	{
		struct FlagData *flagPtr = &flagsman->flagdata[arena].flags[x];
		flagPtr->state    = FLAG_ONMAP;
		flagPtr->freq     = -1;
		flagPtr->carrier  = -1;
	}
	flagsman->UnlockFlagStatus(arena);

	UNLOCK_STATUS(arena);
}


local void dingTimerReset(int arena)
{
	if (ARENA_BAD(arena)) return;
	LOCK_STATUS(arena);
	if (tr)
	{
		// get rid of the current timer (if one exists)
		mainloop->ClearTimer(turfRewardTimer, arena);

		// now create a new timer
		tr[arena].arena = arena;
		tr[arena].dingTime = GTC();
		mainloop->SetTimer(turfRewardTimer, tr[arena].timer_initial, tr[arena].timer_interval, &tr[arena].arena, arena);

		chat->SendArenaSoundMessage(arena, SOUND_BEEP1, "Notice: Reward timer reset. Initial:%i Interval:%i", tr[arena].timer_initial, tr[arena].timer_interval);
	}
	UNLOCK_STATUS(arena);
}


local void LockTurfStatus(int arena)
{
	LOCK_STATUS(arena);
}


local void UnlockTurfStatus(int arena)
{
	UNLOCK_STATUS(arena);
}


local void cleanup(void *arenaPtr)
{
	int *aPtr = arenaPtr;
	int arena = *aPtr;

	mm->ReleaseInterface(tr[arena].trp);
}


local struct FreqInfo* getFreqPtr(int arena, int freq)
{
	struct FreqInfo *fiPtr;
	Link *l;

	for(l = LLGetHead(&tr[arena].freqs) ; l ; l=l->next)
	{
		fiPtr = l->data;
		if(fiPtr->freq == freq)
			return fiPtr;   // found the freq
	}

	// freq didn't exist, lets create it (add it to the linked list)
	fiPtr = amalloc(sizeof(struct FreqInfo));

	fiPtr->freq           = freq;
	fiPtr->numFlags       = 0;
	fiPtr->percentFlags   = 0.0;
	fiPtr->numWeights     = 0;
	fiPtr->percentWeights = 0.0;
	fiPtr->numTags        = 0;
	fiPtr->numRecovers    = 0;
	fiPtr->numLost        = 0;
	fiPtr->numPlayers     = 0;
	fiPtr->perCapita      = 0;
	fiPtr->percent        = 0.0;
	fiPtr->numPoints      = 0;

	LLAddFirst(&tr[arena].freqs, fiPtr);
	return fiPtr;
}

