/* -----------------------------------------------------------------------------
 *
 *  Turf Reward Module for ASSS - an extensible scoring module for Turf Zones
 *                  by GiGaKiLLeR <gigamon@hotmail.com>
 *
 * -----------------------------------------------------------------------------
 *
 * This module began as an effort towards creating what I referred to as
 * "a very specialized version of the Turf Zone reward system"
 *
 * It eventually became apparent that what I really wanted was to create was a
 * way to plug different types of algorithms as scoring methods.  Slowly it has
 * evolved into what you see here.  Separate modules for ASSS can be used to
 * register onto the I_TURFREWARD_POINTS interface, which turf_reward calls.
 * In other words, anyone can write a module that defines a new scoring
 * algorithm, and have turf_reward do the rest.  Additionally, turf_reward
 * comes equipped with a fairly broad range features, including weighted flags,
 * flag recoveries, various other options, and more to come...
 *
 * My greatest thanks to Grelminar for allowing me the chance to do any of this,
 * for taking the time to look over and fix my code, for allowing me to practice
 * my newbie[ish] C programming skills, and much much more.
 *
 * Some Noteworthy Turf Reward System Specs:
 *
 * - Pluggable reward algorithm system
 *     New scoring modules can be written by registering with the
 *     I_TURFREWARD_POINTS interface.  This interface is called by turf_reward.
 *     The CalcReward(...) function (defined by separate scoring modules) is
 *     sent a pointer to the specific arena's turf_reward data when called by
 *     the turf_reward module.  The function can use this data, plus any data
 *     it might have collected on it's own, to calculate scores to be awarded to
 *     players.  These scores should be inputted into the predefined TurfArena
 *     data structure.
 *
 * - Weighted flag system
 *      1. Each flag has a weight.  This weight can change based on the period
 *         of ownership.  The number of weights that can be set and the
 *         corresponding value to each weight can be set through the config.
 *         Through this, the longer a flag is held the more it is "worth".  The
 *         weight settings are not limited to that, however.  In theory is it
 *         possible to set weights to lower over time.  Negative weights are
 *         strictly forbidden, I see no advantage in having them anyways.
 *      2. Recoveries: when a previously owned flag is tagged back before a set
 *         number of dings or period of time elapsed (both can be set using the
 *         conf) the flag's full "worth"/weight prior to losing it is restored.
 *
 *         Example trying to show SOME of the possibilities:
 *         ** Depending on settings in cfg **
 *             My team owns a flag worth 4 WU (weight units) upon next ding. The
 *             enemy team tags that flag.  It is now worth 1 WU upon next ding
 *             to the enemy team.
 *             Case 1: Ding.  The enemy team recieves the 1 WU for that flag
 *                     since they own it.
 *             Case 2: I tag the flag back to my team.  Flag is worth 4 WU as
 *                     before it was lost (NOT 1 WU).
 *                     Ding.  I am rewarded with 4 WU for that flag, and now it
 *                     is worth 5 WU upon next ding.
 *             Case 3: Ding.  The enemy team recieves the 1 WU for that flag
 *                     since they own it.  The flag is now worth 2 WU for the
 *                     enemy team upon next ding. I tag the flag back to my
 *                     team.  The flag, now owned by my team, is again worth
 *                     4 WU as before we lost it to the enemy (NOT 1 WU).  Ding.
 *                     I am rewarded 4 WU for that flag, and now it is worth
 *                     5 WU upon the next ding.  However, note that the enemy
 *                     team has their chance to recover the flag for 2 WU.
 *                     Also, note: the module is not limited to 2 teams having
 *                     a history of previous ownership of a flag (as in this
 *                     example).  There can be an arbitrary number of teams that
 *                     have had previous ownership and the ability to 'recover'
 *                     the flag for their respective previous weight/worth.
 *
 *
 * A couple details on the default turf_reward_points scoring module:
 * (Useful info for those wanting to use the built in scoring module.)
 * - Scoring is not based on # of flags your team owns but rather the # of
 *   weights to those flags (quite a bit more advanced than simple periodic)
 *      The algorithm (TR_REWARD_STD) is:
 *
 *      PerCapita = # of weights per person on team
 *                = (# of weights / # ppl on the team)
 *
 *      Sum up all team's PerCapita's.
 *
 *      Jackpot = # players * 200
 *      NOTE: remember this is for TR_REWARD_STD, each scoring algorithm
 *            can define it's own way of scoring.  I'm not saying that this
 *            is a good way to decide how many points to award, it's likely not
 *            to be.
 *
 *      % of Jackpot to recieve = (PerCapita / Sum of PerCapita's) * 100
 *
 *      Points to be awarded
 *          = ( Jackpot * (% of jackpot to recieve/100) ) / (# ppl on team)
 *
 * - Goals of this scoring method
 *      1.  The more people on a team.  The less points awarded.  All other
 *          variables held constant.  This is an attemptsto eliminate the
 *          incentive to fill your team up to the max, but rather figure out the
 *          optimal # of players (skillwise/teamwise) to get the maximium
 *          benefit/points with respect to the amount of opposition (other teams).
 *      2.  The longer flags are held the more they are worth. (incentive to
 *          "Hold your Turf!").  Territory you already own may be worth more to
 *          keep, rather than losing it while taking over a new area. The
 *          decision completely up to the player as always.
 *      3.  Lost territory can be regained at full worth if reclaimed before a
 *          set number of dings. Additional strategy as certain areas on the map
 *          can be worth more to reclaim/attack.  Again, leaving the door wide
 *          open to the player's decision on what's best to do in each situation.
 *
 * dist: public
 * -----------------------------------------------------------------------------
 */

#include <stdio.h>
#include "asss.h"                /* necessary include to connect the module */
#include "turf_reward.h"
#include "settings/turfreward.h" /* bring in the settings for reward types */


/* easy calls for mutex */
#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))


/* interfaces to various other modules I will probably have to use */
local Imodman     *mm;         /* to get interfaces */
local Iplayerdata *playerdata; /* player data */
#define pd playerdata
local Iarenaman   *arenaman;   /* arena manager, to get conf handle for arena */
local Iflags      *flagsman;   /* to access flag tags and info */
local Iconfig     *config;     /* conf (for arena .conf) services */
local Istats      *stats;      /* stat / score services */
local Ilogman     *logman;     /* logging services */
local Imainloop   *mainloop;   /* main loop - for setting the ding timer */
local Imapdata    *mapdata;    /* get to number of flags in each arena */
local Ichat       *chat;       /* message players */
local Icmdman     *cmdman;     /* for command handling */
local Inet        *net;        /* to send packet for score update */


/* global (to this file) declarations */
local int trkey;  /* turf reward data for every arena */
local int mtxkey; /* to keep things thread safe when accessing TR data */


/* Function Prototypes */
/* Functions connected to callbacks:
 *    arenaAction: arena creation, destruction, and conf changes
 *    flagTag: does everything necessary when a flag is claimed
 *    turfRewardTimer: called when a reward is due to be processed
 */
local void arenaAction(Arena *arena, int action);
local void flagTag(Arena *arena, Player *p, int fid, int oldfreq);
local int turfRewardTimer(void *v);


/* Functions connected to the turf_reward interface:
 *    flagGameReset: resets all flag data for an arena
 *                   (including data in flags module)
 *    dingTimerReset: resets the timer to the specified arena
 *    doReward: forces a reward reward to happen immediately
 */
local void flagGameReset(Arena *arena);
local void dingTimerReset(Arena *arena);
local void doReward(Arena *arena);
local struct TurfArena * GetTurfData(Arena *arena);
local void ReleaseTurfData(Arena *arena);


/* helper / utility functions:
 *    loadSettings: reads the settings for an arena from cfg
 *
 *    clearArenaData: clears out an arena's data
 *                    (not including freq, flag, or numFlags)
 *    clearFreqData: clears and initializes the freq data for a particular arena
 *    clearFlagData: clears and initializes the flag data for a particular arena
 *    calcWeight: figure out how much a flag is worth
 *    preCalc: does a few calculations that will make writing external
 *             calculation modules a lot easier
 *    awardPtr: award pts to each player based on previously done calculations
 *    updateFlags: increment the numDings for all owned flags and recalculate
 *                 their weights
 *    getFreqPtr: get a pointer to a freq.  If it doesn't exist, creates the
 *                freq, and returns a pointer to it.  If freq exists, just
 *                returns pointer to existing one.
 *    cleanup: releases the scoring interface when arena is destroyed
 */
local void loadSettings(Arena *arena);
local void clearArenaData(Arena *arena);
local void clearFreqData(Arena *arena);
local void clearFlagData(Arena *arena, int init);
local int calcWeight(Arena *arena, struct TurfFlag *tf);
local void preCalc(Arena *arena, struct TurfArena *ta);
local void awardPts(Arena *arena, struct TurfArena *ta);
local void updateFlags(Arena *arena);
local struct FreqInfo * getFreqPtr(Arena *arena, int freq);
local void cleanup(void *v);


/* functions for commands */
/* standard user commands:
 *    C_turfTime: to find out how much time till next ding
 *    C_turfIndo: to get settings info on minimum requirements, etc
 */
local void C_turfTime(const char *, Player *, const Target *);
local void C_turfInfo(const char *, Player *, const Target *);


/* mod commands:
 *    C_turfResetFlags: to reset the flag data on all flags
 *    C_forceDing: to force a ding to occur, does not change the timer
 *    C_turfResetTimer: to reset the timer
 */
local void C_turfResetFlags(const char *, Player *, const Target *);
local void C_forceDing(const char *, Player *, const Target *);
local void C_turfResetTimer(const char *, Player *, const Target *);

/* help text for commands */
local helptext_t turftime_help, turfinfo_help, turfresetflags_help,
	turfresettimer_help, forceding_help;


/* connect functions to turf_reward interface */
local Iturfreward _myint =
{
	INTERFACE_HEAD_INIT(I_TURFREWARD, "turfreward-core")
	flagGameReset, dingTimerReset, doReward,
	GetTurfData, ReleaseTurfData
};


EXPORT const char info_turf_reward[]
	= "v0.4.2 by GiGaKiLLeR <gigamon@hotmail.com>";


/* the actual entrypoint into this module */
EXPORT int MM_turf_reward(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD) /* when the module is to be loaded */
	{
		mm = _mm;

		/* get all of the interfaces that we are to use */
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

		/* if any of the interfaces are null then loading failed */
		if (!playerdata || !arenaman || !flagsman || !config
				|| !stats || !logman || !mainloop || !mapdata
				|| !chat || !cmdman || !net)
			return MM_FAIL;

		trkey = arenaman->AllocateArenaData(sizeof(struct TurfArena *));
		mtxkey = arenaman->AllocateArenaData(sizeof(pthread_mutex_t));
		if (trkey == -1 || mtxkey == -1) return MM_FAIL;

		/* special turf_reward commands */
		cmdman->AddCommand("turftime", C_turfTime, turftime_help);
		cmdman->AddCommand("turfinfo", C_turfInfo, turfinfo_help);
		cmdman->AddCommand("forceding", C_forceDing, forceding_help);
		cmdman->AddCommand("turfresetflags", C_turfResetFlags, turfresetflags_help);
		cmdman->AddCommand("turfresettimer", C_turfResetTimer, turfresettimer_help);

		/* register the interface for turf_reward */
		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD) /* when the module is to be unloaded */
	{
		/* unregister the interface for turf_reward */
		if(mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		/* make sure all timers are gone */
		mainloop->CleanupTimer(turfRewardTimer, NULL, cleanup);

		/* get rid of turf_reward commands */
		cmdman->RemoveCommand("turftime", C_turfTime);
		cmdman->RemoveCommand("turfinfo", C_turfInfo);
		cmdman->RemoveCommand("forceding", C_forceDing);
		cmdman->RemoveCommand("turfresetflags", C_turfResetFlags);
		cmdman->RemoveCommand("turfresettimer", C_turfResetTimer);

		arenaman->FreeArenaData(trkey);
		arenaman->FreeArenaData(mtxkey);

		/* release all interfaces */
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

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		/* module only attached to an arena if listed in conf */
		/* create all necessary callbacks */
		mm->RegCallback(CB_ARENAACTION, arenaAction, arena);
		mm->RegCallback(CB_FLAGPICKUP, flagTag, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		/* unregister all the callbacks when detaching arena */
		mm->UnregCallback(CB_ARENAACTION, arenaAction, arena);
		mm->UnregCallback(CB_FLAGPICKUP, flagTag, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


local void arenaAction(Arena *arena, int action)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);

	if (action == AA_CREATE)
	{
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);

		tr = amalloc(sizeof(*tr));
		if(!tr)
			return; /* cannot allocate memory */
		*p_tr = tr;

		tr->reward_style         = 0;
		tr->min_players_on_freq  = 0;
		tr->min_players_in_arena = 0;
		tr->min_teams            = 0;
		tr->min_flags            = 0;
		tr->min_percent_flags    = 0;
		tr->min_weights          = 0;
		tr->min_percent_weights  = 0;
		tr->min_percent          = 0;
		tr->jackpot_modifier     = 0;
		tr->recover_dings        = 0;
		tr->set_weights          = 0;
		tr->weights              = NULL;

		tr->dingTime       = current_ticks();
		tr->timer_initial  = 0;
		tr->timer_interval = 0;
		tr->trp            = NULL;

		tr->numFlags = 0;
		clearArenaData(arena);
		tr->flags = NULL;
	}

	tr = *p_tr;

	LOCK_STATUS(arena);

	if (action == AA_CREATE)
	{
		loadSettings(arena);

		tr->numFlags = mapdata->GetFlagCount(arena);
		clearArenaData(arena);

		/* create and initialize all the flags */
		tr->flags = amalloc(tr->numFlags * sizeof(struct TurfFlag));
		if(!tr->flags)
			return; /* cannot allocate memory */
		clearFlagData(arena, 1);

		/* create and intialize the data on freqs */
		LLInit(&tr->freqs);

		tr->trp = mm->GetInterface(I_TURFREWARD_POINTS, arena);

		/* set up the timer for arena */
#if 0
		if(tr->multi_arena_id)
		{
			/* multi arena enabled, use the id as the key for the timer */
			tr->arena = arena;
			tr->dingTime = current_ticks();
			mainloop->SetTimer(turfRewardTimer, tr->timer_initial,
				tr->timer_interval, &tr->arena, tr->multi_arena_id);
		}
		else
#endif
		{
			/* single arena only */
			tr->dingTime = current_ticks();
			mainloop->SetTimer(turfRewardTimer, tr->timer_initial,
				tr->timer_interval, arena, arena);
		}
	}
	else if (action == AA_DESTROY)
	{
		/* clear old timer and cleanup the I_TURFREWARD_POINTS interface */
		mainloop->CleanupTimer(turfRewardTimer, arena, cleanup);

		/* clean up any old arena data */
		clearArenaData(arena);

		/* if there is existing weights data, discard */
		if (tr->weights)
		{
			afree(tr->weights);
			tr->weights = NULL;
		}

		/* if there is existing flags data, discard */
		if (tr->flags)
		{
			clearFlagData(arena, 0);
			afree(tr->flags);
			tr->flags = NULL;
		}

		/* if there is existing freqs data, discard */
		clearFreqData(arena);
	}
	else if (action == AA_CONFCHANGED)
	{
		int initial  = tr->timer_initial;
		int interval = tr->timer_interval;
		loadSettings(arena);
		if( (initial!=tr->timer_initial) || (interval!=tr->timer_interval) )
		{
			chat->SendArenaMessage(arena,
				"Ding timer settings in config was changed. Updating...");
			dingTimerReset(arena);
		}
	}

	UNLOCK_STATUS(arena);

	if (action == AA_DESTROY)
	{
		afree(tr);
		*p_tr = NULL;
		pthread_mutex_destroy((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey));
	}
}


local void loadSettings(Arena *arena)
{
	ConfigHandle c = arena->cfg;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	/* cfghelp: TurfReward:RewardStyle, arena, enum, def: $REWARD_STD
	 * The reward algorithm to be used.  Default is $REWARD_STD for
	 * standard weighted scoring. Other built in algorithms are:
	 * $REWARD_DISABLED: disable scoring, $REWARD_PERIODIC: normal
	 * periodic scoring but with the stats, $REWARD_FIXED_PTS: each
	 * team gets a fixed # of points based on 1st, 2nd, 3rd,... place
	 * $REWARD_STD_MULTI: standard weighted scoring + this arena is
	 * scored along with other arenas simulanteously.  Note: currently
	 * only $REWARD_STD and $REWARD_PERIODIC are implemented. */
	tr->reward_style = config->GetInt(c, "TurfReward", "RewardStyle", REWARD_STD);
#if 0
	/* cfghelp: TurfReward:MultiArenaID, arena, int, def: 0
	 * Used for multi-arena (cross arena) scoring only.  Defines the
	 * set of arenas the arena is associated with (parallels the
	 * idea of multicast addresses in networking).  If this arena is
	 * not using a multi-arena scoring method, set to 0
	 * (or simply remove this setting from the conf).
	 * Note: MultiArena scoring is not currently implemented */
	tr->multi_arena_id = (void*)config->GetInt(c, "TurfReward", "MultiArenaID", 0);
#endif
	/* cfghelp: TurfReward:MinPlayersFreq, arena, int, def: 3
	 * The minimum number of players needed on a freq for that
	 * team to be eligable to recieve points. */
	tr->min_players_on_freq = config->GetInt(c, "TurfReward", "MinPlayersFreq", 3);

	/* cfghelp: TurfReward:MinPlayersArena, arena, int, def: 6
	 * The minimum number of players needed in the arena for anyone
	 * to be eligable to recieve points. */
	tr->min_players_in_arena = config->GetInt(c, "TurfReward", "MinPlayersArena", 6);

	/* cfghelp: TurfReward:MinTeams, arena, int, def: 2
	 * The minimum number of teams needed in the arena for anyone
	 * to be eligable to recieve points. */
	tr->min_teams = config->GetInt(c, "TurfReward", "MinTeams", 2);

	/* cfghelp: TurfReward:MinFlags, arena, int, def: 1
	 * The minimum number of flags needed to be owned by a freq for
	 * that team to be eligable to recieve points. */
	tr->min_flags = config->GetInt(c, "TurfReward", "MinFlags", 1);

	/* cfghelp: TurfReward:MinFlagsPercent, arena, int, def: 0
	 * The minimum percent of flags needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	tr->min_percent_flags
		= (double)config->GetInt(c, "TurfReward", "MinFlagsPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:MinWeights, arena, int, def: 1
	 * The minimum number of weights needed to be owned by a freq for
	 * that team to be eligable to recieve points. */
	tr->min_weights = config->GetInt(c, "TurfReward", "MinWeights", 1);

	/* cfghelp: TurfReward:MinWeightsPercent, arena, int, def: 0
	 * The minimum percent of weights needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	tr->min_percent_weights
		= (double)config->GetInt(c, "TurfReward", "MinWeightsPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:MinPercent, arena, int, def: 0
	 * The minimum percent of points needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	tr->min_percent = (double)config->GetInt(c, "TurfReward", "MinPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:JackpotModifier, arena, int, def: 200
	 * Modifies the number of points to award.  Meaning varies based on reward
	 * algorithm being used.
	 * For $REWARD_STD: jackpot = JackpotModifier * # players */
	tr->jackpot_modifier = config->GetInt(c, "TurfReward", "JackpotModifier", 200);

	/* cfghelf: TurfReward:MaxPoints, arena, int, def: 10000
	 * The maximum number of points allowed to be award to a single player per
	 * ding. If a player's points is calculated to be ablove the max, only this
	 * amount will be awarded. */
	tr->max_points = config->GetInt(c, "TurfReward", "MaxPoints", 10000);

	/* cfghelp: TurfReward:TimerInitial, arena, int, def: 6000
	 * Inital turf_reward ding timer period. */
	tr->timer_initial = config->GetInt(c, "TurfReward", "TimerInitial", 6000);

	/* cfghelp: TurfReward:TimerInterval, arena, int, def: 6000
	 * Subsequent turf_reward ding timer period. */
	tr->timer_interval = config->GetInt(c, "TurfReward", "TimerInterval", 6000);

	/* cfghelp: TurfReward:RecoveryCutoff, arena, enum, def: $TR_RECOVERY_DINGS
	 * Style of recovery cutoff to be used.
	 * $TR_RECOVERY_DINGS - recovery cutoff based on RecoverDings.
	 * $TR_RECOVERY_TIME - recovery cutoff based on RecoverTime.
	 * $TR_RECOVERY_DINGS_AND_TIME - recovery cutoff based on both RecoverDings
	 * and RecoverTime. */
	tr->recovery_cutoff
		= config->GetInt(c, "TurfReward", "RecoveryCutoff", TR_RECOVERY_DINGS);

	/* cfghelp: TurfReward:RecoverDings, arena, int, def: 1
	 * After losing a flag, the number of dings allowed to pass before a freq
	 * loses the chance to recover.  0 means you have no chance of recovery
	 * after it dings (to recover, you must recover before any ding occurs),  1
	 * means it is allowed to ding once and you still have a chance to recover
	 * (any ding after that you lost chance of full recovery), ... */
	tr->recover_dings = config->GetInt(c, "TurfReward", "RecoverDings", 1);

	/* cfghelp: TurfReward:RecoverTime, arena, int, def: 300
	 * After losing a flag, the time (seconds) allowed to pass before a freq
	 * loses the chance to recover. */
	tr->recover_time = config->GetInt(c, "TurfReward", "RecoverTime", 300);

	/* cfghelp: TurfReward:RecoverMax, arena, int, def: -1
	 * Maximum number of times a flag may be recovered. (-1 means no max) */
	tr->recover_max = config->GetInt(c, "TurfReward", "RecoverMax", -1);

	/* cfghelp: TurfReward:WeightCalc, arena, enum, def: $TR_WEIGHT_DINGS
	 * The method weights are calculated.  $TR_WEIGHT_TIME means each weight
	 * stands for one minute (ex: Weight004 is the weight for a flag owned for
	 * 4 minutes.  $TR_WEIGHT_DINGS means each weight stands for one ding of
	 * ownership (ex: Weight004 is the weight for a flag that was owned during
	 * 4 dings */
	tr->weight_calc
		= config->GetInt(c, "TurfReward", "WeightCalc", TR_WEIGHT_DINGS);

	/* cfghelp: TurfReward:SetWeights, arena, int, def: 0
	 * How many weights to set from cfg (16 means you want to specify Weight0 to
	 * Weight15). If set to 0, then by default one weight is set with a value
	 * of 1. */
	if (tr->weights)
	{
		/* there is existing weights data, discard */
		afree(tr->weights);
		tr->weights = NULL;
	}
	tr->set_weights = config->GetInt(c, "TurfReward", "SetWeights", 0);

	if(tr->set_weights<1)
	{
		/* user didn't set the weights, just set a weight with 1 WU then */
		tr->set_weights = 1;
		tr->weights = amalloc(sizeof(int));
		if(!tr->weights)
			return;	/* cannot allocate memory */
		tr->weights[0]=1;
	}
	else
	{
		int x;
		char wStr[] = "Weight####";
		int defaultVal = 1;  /* to keep it non-decreasing and non-increasing */
		                     /* if conf didn't specify a weight */

		if (tr->set_weights>100)
			tr->set_weights = 100;

		tr->weights = amalloc(tr->set_weights * sizeof(int));
		if(!tr->weights)
			return; /* cannot allocate memory */

		for(x=0 ; x<tr->set_weights ; x++)
		{
			sprintf(wStr, "Weight%d", x);
			tr->weights[x] = config->GetInt(c, "TurfReward", wStr, defaultVal);
			defaultVal = tr->weights[x];
		}
	}

	/* now that settings are read in
	 * check for possible problems, adjust if necessary */
	if(tr->min_players_on_freq < 1)
		tr->min_players_on_freq = 1;
	if(tr->min_players_in_arena < 1)
		tr->min_players_in_arena = 1;
	if(tr->min_teams < 1)
		tr->min_teams = 1;
	if(tr->min_flags < 1)
		tr->min_flags = 1;
	if(tr->min_weights < 1)
		tr->min_weights =1;
	if(tr->timer_initial < 1500)
		tr->timer_initial = 1500;
	if(tr->timer_interval < 1500)  /* 15 second safety so that server cannot */
		tr->timer_interval = 1500; /* be overloaded by dings */
}


local void clearArenaData(Arena *arena)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	tr->numPlayers    = 0;
	tr->numTeams      = 0;
	tr->numWeights    = 0;
	tr->numPoints     = 0;
	tr->sumPerCapitas = 0;
	tr->numTags       = 0;
	tr->numLost       = 0;
	tr->numRecovers   = 0;
}


local void clearFreqData(Arena *arena)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	LLEnum(&tr->freqs, afree);
	LLEmpty(&tr->freqs);
}


local void clearFlagData(Arena *arena, int init)
{
	int x;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	for(x=0 ; x<tr->numFlags ; x++)
	{
		struct TurfFlag *ptr = &tr->flags[x];

		ptr->dings     = -1;
		ptr->freq      = -1;
		ptr->weight    =  0;
		ptr->taggerPID = -1;

		/* now clear out the linked list 'old' */
		if (init)
			LLInit(&ptr->old);
		LLEnum(&ptr->old, afree);
		LLEmpty(&ptr->old);
	}
}


local void flagTag(Arena *arena, Player *p, int fid, int oldfreq)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	int freq=-1;
	int r_freq=-1, r_dings, r_weight, r_pid, r_rec, r_tc, /* flag recover data */
	    l_freq=-1, l_dings, l_weight, l_pid, l_rec, l_tc; /* flag lost data */
	struct TurfFlag *pTF   = NULL;  /* pointer to turf flag that was tagged */
	struct OldNode  *oPtr  = NULL;  /* pointer to node of linked list holding */
	                                /* previous owners */
	struct FreqInfo *pFreq = NULL;  /* pointer to freq that tagged flag */

	Link *l = NULL;  /* pointer to a node in the linked list */

	if (!arena || !*p_tr) return; else tr = *p_tr;

	if(fid < 0 || fid >= tr->numFlags)
	{
		logman->LogP(L_MALICIOUS, "turf_reward", p,
			"nonexistent flag tagged: %d not in 0..%d",
			fid, tr->numFlags-1);
		return;
	}

	LOCK_STATUS(arena);

	freq = p->p_freq;
	pTF = &tr->flags[fid];

	if(pTF->freq==freq)
	{
		/* flag was already owned by that team */
		UNLOCK_STATUS(arena);
		logman->LogP(L_MALICIOUS, "turf_reward", p,
			"flag tagged was already owned by player's team.");
		return;
	}

	/* increment number of flag tags */
	pFreq = getFreqPtr(arena, freq);
	pFreq->numTags++;
	tr->numTags++;

	if(pTF->freq>-1)
	{
		/* flag that was tagged was owned by another team */
		struct FreqInfo *pFreqOld;

		oPtr = amalloc(sizeof(struct OldNode));
		if(!oPtr)
			return; /* cannot allocate memory */
		oPtr->lastOwned = 0;
		oPtr->freq      = l_freq   = pTF->freq;
		oPtr->dings     = l_dings  = pTF->dings;
		oPtr->weight    = l_weight = pTF->weight;
		oPtr->taggerPID = l_pid    = pTF->taggerPID;
		oPtr->recovered = l_rec    = pTF->recovered;
		oPtr->tagTC     = l_tc     = pTF->tagTC;
		oPtr->lostTC    = current_ticks();  /* time flag was lost */

		/* flag was lost, that freq now gets a chance for recovery (add into
		 * linked list).  Since this team owned the flag, there is no way it
		 * can already be listed in linked list so simply add it in */
		if(tr->recover_max == -1 || l_rec < tr->recover_max)
			LLAddFirst(&pTF->old, oPtr);

		/* increment number of flag losses */
		pFreqOld = getFreqPtr(arena, l_freq);
		pFreqOld->numLost++;
		tr->numLost++;
	}

	/* search for matching freq in list of teams that have chance to recover */
	for(l = LLGetHead(&pTF->old); l; l = l->next)
	{
		oPtr = l->data;

		if(oPtr->freq == freq)
		{
			/* lastOwned for each entry is incremented and checked during each
			 * ding, meaning all nodes are already assured to be valid for
			 * recovery based on dings.  However, we must still check if an
			 * entry is still valid for recovery based on time past since lost */
			if(tr->recovery_cutoff==TR_RECOVERY_TIME
				|| tr->recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
			{
				if ( (TICK_DIFF(current_ticks(), oPtr->lostTC) / 100)
					> tr->recover_time )
				{
					LLRemove(&pTF->old, oPtr);
					afree(oPtr);
					break; /* break out of the for loop */
				}
			}

			/* found entry that matches freq, meaning flag was recovered */
			r_freq   = pTF->freq      = freq;
			r_dings  = pTF->dings     = oPtr->dings;
			r_weight = pTF->weight    = oPtr->weight;

			/* pid of player who recovered flag now gets taggerPID */
			r_pid    = pTF->taggerPID = p->pid;

			/* increment # of times flag was recovered */
			r_rec    = pTF->recovered = oPtr->recovered + 1;

			/* restore old time */
			r_tc     = pTF->tagTC     = oPtr->tagTC;

			/* remove node from linked list */
			LLRemove(&pTF->old, oPtr);
			afree(oPtr);

			/* increment number of flag recoveries */
			pFreq->numRecovers++;
			tr->numRecovers++;

			break;  /* break out of the loop */
		}
	}

	if(r_freq==-1)
	{
		/* flag wasn't recovered, fill in data for newly tagged flag */
		pTF->freq      = freq;
		pTF->dings     = 0;
		pTF->weight    = calcWeight(arena, pTF);
		pTF->taggerPID = p->pid;
		pTF->recovered = 0;
		pTF->tagTC     = current_ticks();
	}
	pTF->lastTC = current_ticks();

	UNLOCK_STATUS(arena);

	logman->LogP(L_DRIVEL, "turf_reward", p, "flag was tagged");

	/* finally do whatever callbacks are necessary */
	DO_CBS(CB_TURFTAG, arena, TurfTagFunc, (arena, p, fid));
	if(r_freq!=-1)
		DO_CBS(CB_TURFRECOVER, arena, TurfRecoverFunc,
			(arena, fid, r_pid, r_freq, r_dings, r_weight, r_rec));
	if(l_freq!=-1)
		DO_CBS(CB_TURFLOST, arena, TurfLostFunc,
			(arena, fid, l_pid, l_freq, l_dings, l_weight, l_rec));
}


local int calcWeight(Arena *arena, struct TurfFlag *tf)
{
	int weightNum = 0;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return 0; else tr = *p_tr;

	switch(tr->weight_calc)
	{
	case TR_WEIGHT_TIME:
		/* calculate by time owned (minutes) */
		weightNum = (TICK_DIFF(current_ticks(), tf->tagTC) / 100) / 60;
		break;
	case TR_WEIGHT_DINGS:
		/* calculate by # of dings */
		weightNum = tf->dings;
		break;
	default:
		/* setting not understood */
		logman->LogA(L_DRIVEL, "turf_reward", arena,
			"bad setting for WeightCalc:%d", tr->weight_calc);
		return 0;
	}

	if (weightNum < 0)
		return 0;
	if (weightNum > tr->set_weights-1)
		return tr->weights[tr->set_weights-1];
	return tr->weights[weightNum];
}


local int turfRewardTimer(void *v)
{
	Arena *arena = v;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return FALSE; else tr = *p_tr;

	if (!arena) return FALSE;

	/* we have a go for starting the reward sequence */
	doReward(arena);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "timer ding");

	tr->dingTime = current_ticks();
	return TRUE;  /* yes, we want timer called again */
}


local void doReward(Arena *arena)
{
	trstate_t action;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	LOCK_STATUS(arena);

	if (!tr || !tr->flags)
	{
		UNLOCK_STATUS(arena);
		return;
	}

	/* do pre-calculations (to make it easier for ppl to write a score
	 * calculation interface).  This ensures that all teams that own a flag
	 * (doesn't have to have players) are in the linked list tr[x].freqs and
	 * also that all teams with people on them are in the linked list.
	 * Therefore, all freqs that need scoring are already in the linked list
	 * before calling the scoring interface. */
	preCalc(arena, tr);

	/* calculate the points to award using interface for score calculations */
	if (tr->trp)
		action = tr->trp->CalcReward(arena,tr);
	else
		action = TR_FAIL_CALCULATIONS;

	/* check if we are to award points */
	if (action==TR_AWARD_UPDATE || action==TR_AWARD_ONLY)
	{
		awardPts(arena, tr);
		stats->SendUpdates();
	}

	/* do the callback for post-reward (WHILE LOCK IS IN PLACE)
	 * Note: this might become a problem if functions registered with the
	 *        callback require a large amount of processing time */
	DO_CBS(CB_TURFPOSTREWARD, arena, TurfPostRewardFunc, (arena, tr));

	/* check if we are to update flags */
	if (action==TR_AWARD_UPDATE
			|| action==TR_UPDATE_ONLY
			|| action==TR_FAIL_REQUIREMENTS)
		updateFlags(arena);

	/* new freq data for next round */
	clearFreqData(arena); /* intialize the data on freqs */

	UNLOCK_STATUS(arena);
}


local void preCalc(Arena *arena, struct TurfArena *tr)
{
	struct FreqInfo *pFreq;
	Player *pdPtr;
	Link *link;
	int x;

	/* make sure these are clear (they should be already) */
	tr->numPlayers    = 0;
	tr->numPoints     = 0;
	tr->numTeams      = 0;
	tr->numWeights    = 0;
	tr->sumPerCapitas = 0;

	/* go through all flags and if owned, update numFlags and numWeight for
	 * the corresponding freq */
	for(x=0 ; x<tr->numFlags ; x++)
	{
		int freq, dings, weight;
		struct TurfFlag *flagPtr = &tr->flags[x];

		freq   = flagPtr->freq;
		dings  = flagPtr->dings;
		weight = flagPtr->weight;

		if (freq>=0 && dings>=0 && weight>=0)
		{
			/* flag is owned */
			pFreq = getFreqPtr(arena, freq);  /* get pointer to freq */
			pFreq->numFlags++;
			pFreq->numWeights+=weight;
			tr->numWeights+=weight;
		}
	}

	/* go through all players and update freq info on numPlayers */
	playerdata->Lock();
	FOR_EACH_PLAYER(pdPtr)
	{
		if ( (pdPtr->arena==arena)
			&& (pdPtr->p_ship!=SPEC)
			&& (pdPtr->status==S_PLAYING) )
		{
			pFreq = getFreqPtr(arena, pdPtr->p_freq);
			pFreq->numPlayers++;
			tr->numPlayers++;
		}
	}
	playerdata->Unlock();
}


local void awardPts(Arena *arena, struct TurfArena *tr)
{
	Link *link;
	Player *x;

	/* this is where we award each player that deserves points */
	playerdata->Lock();
	FOR_EACH_PLAYER(x)
	{
		if (x->arena == arena)
		{
			unsigned int points;

			/* player is in arena */
			if ((x->p_ship == SPEC) || (x->status!=S_PLAYING))
			{
				/* player is in spec/not playing */
				chat->SendSoundMessage(x, SOUND_DING, "Reward: 0 (not playing)");
			}
			else if ( x->position.status & STATUS_SAFEZONE )
			{
				chat->SendSoundMessage(x, SOUND_DING, "Reward: 0 (in safe zone)");
			}
			else if ( (points = (getFreqPtr(arena, x->p_freq))->numPoints) )
			{
				/* player is on a freq that recieved points */
				if(points > tr->max_points)  /* only award up to MaxPoints */
					points = tr->max_points;

				/* award player */
				stats->IncrementStat(x, STAT_FLAG_POINTS, points);

				/* notify player of reward
				 * TODO: allow scoring module to define output */
				chat->SendSoundMessage(x, SOUND_DING, "Reward: %i", points);
			}
			else
			{
				/* player is on a freq that didn't recieve points */
				chat->SendSoundMessage(x, SOUND_DING,
					"Reward: 0 (reward requirements not met)");
			}
		}
	}
	playerdata->Unlock();
}


local void updateFlags(Arena *arena)
{
	int x;
	struct TurfFlag *flags;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);

	if (!arena || !*p_tr) return; else tr = *p_tr;
	flags = tr->flags;

	/* increment numdings and weights for all owned flags */
	for(x=0 ; x<tr->numFlags ; x++)
	{
		struct TurfFlag *flagPtr = &flags[x];
		struct OldNode *oPtr;
		Link *l, *next;

		if (flagPtr->freq != -1)
		{
			/* flag is owned, increment # dings and update weight accordingly */
			flagPtr->dings++;
			flagPtr->weight=calcWeight(arena, flagPtr);
		}

		/* increment lastOwned for every node (previous owners that can recover) */
		for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
		{
			next = l->next;
			oPtr = l->data;
			oPtr->lastOwned++;
		}

		if(tr->recovery_cutoff==TR_RECOVERY_DINGS
			|| tr->recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
		{
			/* remove entries for teams that lost the chance to recover
			 * (setting based on dings) */
			for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
			{
				next = l->next;
				oPtr = l->data;
				/* check if lastOwned is within limits */
				if(oPtr->lastOwned > tr->recover_dings)
				{
					/*entry for team that lost chance to recover flag */
					LLRemove(&flagPtr->old, oPtr);
					afree(oPtr);
					continue;
				}
			}
		}
		if(tr->recovery_cutoff==TR_RECOVERY_TIME
			|| tr->recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
		{
			/* remove entries for teams that lost the chance to recover
			 * (setting based on time) */
			for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
			{
				next = l->next;
				oPtr = l->data;

				/* check if lostTC - current time
				 * (time since flag was lost) is still within limits */
				if( (TICK_DIFF(current_ticks(), oPtr->lostTC) / 100)
					> tr->recover_time)
				{
					LLRemove(&flagPtr->old, oPtr);
					afree(oPtr);
					continue;
				}
			}
		}
	}
}


local helptext_t turftime_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the amount of time till next ding.\n";
local void C_turfTime(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	unsigned int days, hours, minutes, seconds;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	/* TODO: change to 00:00:00 format */

	LOCK_STATUS(arena);
	seconds = (tr->timer_interval - TICK_DIFF(current_ticks(), tr->dingTime)) / 100;
	UNLOCK_STATUS(arena);

	if (seconds!=0)
	{
		if ( (minutes = (seconds / 60)) )
		{
			seconds = seconds % 60;
			if ( (hours = (minutes / 60)) )
			{
				minutes = minutes % 60;
				if ( (days = (hours / 24)) )
				{
					hours = hours % 24;
					chat->SendMessage(p,
						"Next ding in: %d days %d hours %d minutes %d seconds.",
						days,
						hours,
						minutes,
						seconds);
				}
				else
					chat->SendMessage(p,
						"Next ding in: %d hours %d minutes %d seconds.",
						hours,
						minutes,
						seconds);
			}
			else
				chat->SendMessage(p,
					"Next ding in: %d minutes %d seconds.",
					minutes,
					seconds);
		}
		else
			chat->SendMessage(p, "Next ding in: %d seconds.", seconds);
	}
}


local helptext_t turfinfo_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the current settings / requirements to recieve awards.\n";
local void C_turfInfo(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	LOCK_STATUS(arena);
	chat->SendMessage(p, "+---- Arena Requirements ----+");
	chat->SendMessage(p, "   Minimum players: %d", tr->min_players_in_arena);
	chat->SendMessage(p, "     Minimum teams: %d", tr->min_teams);
	chat->SendMessage(p, "+---- Team Requirements  ----+");
	chat->SendMessage(p, "   Minimum players: %d", tr->min_players_on_freq);
	chat->SendMessage(p, "   Minimum # flags: %d", tr->min_flags);
	chat->SendMessage(p, "   Minimum %% flags: %f", tr->min_percent_flags);
	chat->SendMessage(p, " Minimum # weights: %d", tr->min_weights);
	chat->SendMessage(p, " Minimum %% weights: %f", tr->min_percent_weights);
	chat->SendMessage(p, "  Jackpot modifier: %d", tr->jackpot_modifier);
	chat->SendMessage(p, "+---- Misc. Useful Info  ----+");
	chat->SendMessage(p, "        Ding every: %d seconds", tr->timer_interval/100);
	chat->SendMessage(p, "   Recovery cutoff: %d dings", tr->recover_dings);
	UNLOCK_STATUS(arena);
}


local helptext_t turfresetflags_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the turf_reward module's and flags module's flag data in your current arena.\n";
local void C_turfResetFlags(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	flagGameReset(arena);  /* does the locking for us already */
}


local helptext_t forceding_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Forces a reward to take place immediately in your current arena.\n";
local void C_forceDing(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	doReward(arena);  /* does the locking for us already */
}


local helptext_t turfresettimer_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the ding timer in your current arena.\n";
local void C_turfResetTimer(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	dingTimerReset(arena);  /* does the locking for us already */
}


local void flagGameReset(Arena *arena)
{
	int x;
	ArenaFlagData *afd;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	LOCK_STATUS(arena);

	/* clear the data in this module */
	if (tr && tr->flags)
		clearFlagData(arena, 0);

	/* clear the data in the flags module */
	afd = flagsman->GetFlagData(arena);
	for(x=0 ; x<afd->flagcount ; x++)
	{
		struct FlagData *flagPtr = &afd->flags[x];
		flagPtr->state    = FLAG_ONMAP;
		flagPtr->freq     = -1;
		flagPtr->carrier  = NULL;
	}
	flagsman->ReleaseFlagData(arena);

	UNLOCK_STATUS(arena);
}


local void dingTimerReset(Arena *arena)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;

	LOCK_STATUS(arena);
	if (tr)
	{
		/* get rid of the current timer (if one exists) */
		mainloop->ClearTimer(turfRewardTimer, arena);

		/* now create a new timer */
		tr->dingTime = current_ticks();
		mainloop->SetTimer(turfRewardTimer, tr->timer_initial,
			tr->timer_interval, arena, arena);

		chat->SendArenaSoundMessage(arena, SOUND_BEEP1,
			"Notice: Reward timer reset. Initial:%i Interval:%i",
			tr->timer_initial, tr->timer_interval);
	}
	UNLOCK_STATUS(arena);
}


local struct TurfArena * GetTurfData(Arena *arena)
{
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return NULL; else tr = *p_tr;
	LOCK_STATUS(arena);
	return tr;
}


local void ReleaseTurfData(Arena *arena)
{
	UNLOCK_STATUS(arena);
}


local void cleanup(void *v)
{
	Arena *arena = v;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return; else tr = *p_tr;
	mm->ReleaseInterface(tr->trp);
}


local struct FreqInfo* getFreqPtr(Arena *arena, int freq)
{
	struct FreqInfo *fiPtr;
	Link *l;
	struct TurfArena *tr, **p_tr = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_tr) return NULL; else tr = *p_tr;

	for(l = LLGetHead(&tr->freqs) ; l ; l=l->next)
	{
		fiPtr = l->data;
		if(fiPtr->freq == freq)
			return fiPtr;   /* found the freq */
	}

	/* freq didn't exist, lets create it (add it to the linked list) */
	fiPtr = amalloc(sizeof(struct FreqInfo));
	if(!fiPtr)
		return NULL; /* cannot allocate memory */

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

	LLAddFirst(&tr->freqs, fiPtr);
	return fiPtr;
}

