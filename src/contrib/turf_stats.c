
/*
 * Turf Statistics Module for ASSS
 * - gathers and records data from turf_reward
 *
 * TODO:
 * 1.  extend module to handle indiviual player stats, right now it only does team stats
 *     Note: before I do individual stats, I would like to modify turf_reward to support it first
 * 2.  extend module to handle commands with targets (for example,  a command to get the
 *     server to output you team's stats to your team chat)
 * 3.  make output look nice :)
 * 4.  an idea I have is to set a short timer after data has been recorded to time arena output so
 *     that postReward returns as soon as possible, letting turf_reward finish up and unlock asap.
 *     otherwise, how it is now, turf_reward is waiting for all the stats to be outputted before
 *     it can process any new ongoing events since turf_reward is locked.
 */

#include "asss.h"
#include "turf_reward.h"

// easy calls for mutex
#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))

local Imodman     *mm;
local Iplayerdata *playerdata;          // player data
#define pd playerdata
local Iarenaman   *arenaman;            // arena manager
local Iconfig     *config;              // config (for arena .cfg) services
local Ilogman     *logman;              // logging services
local Ichat       *chat;                // message players
local Icmdman     *cmdman;              // for command handling

struct TurfStatsData
{
	int numFlags;
	int numPlayers;
	int numTeams;
	long int numWeights;
	unsigned long int numPoints;
	double sumPerCapitas;

	unsigned int numTags;
	unsigned int numLost;
	unsigned int numRecovers;

	LinkedList freqs; // linked list of FreqInfo structs
};

struct TurfStats
{
	// config settings
	int maxHistory;
	int statsOnDing;

	// stats data
	int dingCount;
	int numStats;
	LinkedList stats;  // linked list of TurfStatsInfo structs
                           // idea is to add a link to the front and remove from the end
};

local int tskey;  // key to turf stats data
local int mtxkey; // key to turf stats mutexes

/* function prototypes */
// connected to callbacks
local void postReward(Arena *arena, struct TurfArena *ta);
local void arenaAction(Arena *arena, int action);

/* comands */
local helptext_t turfstats_help, forcestats_help;
// standard user commands
local void C_turfStats(const char *, Player *, const Target *);    // to get stats for all freqs
// mod commands
local void C_forceStats(const char *, Player *, const Target *);   // to force stats to be outputted on the spot

// helper functions
local void clearHistory(Arena *arena);
local void ADisplay(Arena *arena, int histNum);
local void PDisplay(Arena *arena, Player *pid, int histNum);
//local struct FreqInfo* findFreqPtr(Arena *arena, int histNum, int freq);

EXPORT const char info_turf_stats[] = "v0.2.0 by GiGaKiLLeR <gigamon@hotmail.com>";

EXPORT int MM_turf_stats(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)  // when the module is to be loaded
	{
		mm = _mm;	

		// get all of the interfaces that we are to use
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		arenaman   = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		config     = mm->GetInterface(I_CONFIG,     ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		cmdman     = mm->GetInterface(I_CMDMAN,     ALLARENAS);

		// if any of the interfaces are null then loading failed
		if (!playerdata || !arenaman || !config || !logman || !chat || !cmdman)
			return MM_FAIL;

		tskey = arenaman->AllocateArenaData(sizeof(struct TurfStats *));
		mtxkey = arenaman->AllocateArenaData(sizeof(pthread_mutex_t));
		if (tskey == -1 || mtxkey == -1) return MM_FAIL;

		cmdman->AddCommand("turfstats", C_turfStats, turfstats_help);
		cmdman->AddCommand("forcestats", C_forceStats, forcestats_help);
		
		return MM_OK;
	}
	else if (action == MM_UNLOAD)  // when the module is to be unloaded
	{
		cmdman->RemoveCommand("turfstats", C_turfStats);
		cmdman->RemoveCommand("forcestats", C_forceStats);

		arenaman->FreeArenaData(tskey);
		arenaman->FreeArenaData(mtxkey);		

		// release all interfaces
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(arenaman);
		mm->ReleaseInterface(config);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmdman);

		return MM_OK;
	}
	else if (action == MM_ATTACH)  // module only attached to an arena if listed in conf
	{
		// create all necessary callbacks
		mm->RegCallback(CB_ARENAACTION, arenaAction, arena);
		mm->RegCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		// unregister all the callbacks
		mm->UnregCallback(CB_ARENAACTION, arenaAction, arena);
		mm->UnregCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

local void arenaAction(Arena *arena, int action)
{
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	ts = *p_ts;

	if (action == AA_CREATE)
	{
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);
	}

	LOCK_STATUS(arena);

	if (action == AA_CREATE)
	{
		ConfigHandle c = arena->cfg;
		ts->maxHistory = config->GetInt(c, "TurfStats", "MaxHistory", 0);
		if (ts->maxHistory<0)
			ts->maxHistory = 0;
		ts->statsOnDing = config->GetInt(c, "TurfStats", "StatsOnDing", 1);
		if (ts->statsOnDing<1)
			ts->statsOnDing=1;

		ts->dingCount = 0;
		ts->numStats = 0;
		LLInit(&ts->stats);         // initalize list of stats
	}
	else if (action == AA_DESTROY)
	{
		// free history array
		clearHistory(arena);

		// might as well, just in case...
		ts->numStats = 0;
		ts->dingCount = 0;
	}
	else if (action == AA_CONFCHANGED)
	{
		int newMaxHistory;
		ConfigHandle c = arena->cfg;

		ts->statsOnDing = config->GetInt(c, "TurfStats", "StatsOnDing", 1);
		if (ts->statsOnDing<1)
			ts->statsOnDing=1;

		newMaxHistory = config->GetInt(c, "TurfStats", "MaxHistory", 0);
		if (newMaxHistory < 0)
			newMaxHistory = 0;  // max history must be >= 0
		if (newMaxHistory != ts->maxHistory)
		{
			ts->maxHistory = newMaxHistory;

			// erase history
			clearHistory(arena);
		}
	}

	UNLOCK_STATUS(arena);

	if (action == AA_DESTROY)
	{
		pthread_mutex_destroy((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey));
	}
}

local void postReward(Arena *arena, struct TurfArena *ta)
{
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l = NULL;
	struct TurfStatsData *tsd;

	if (!arena || !*p_ts) return; else ts = *p_ts;
	
	LOCK_STATUS(arena);
	
	if (ts->numStats >= ts->maxHistory)  // if we already have the maximum # of histories
	{
		// erase oldest history (end of linked list)
		Link *nextL = NULL;
		struct TurfStatsData *data;

		for(nextL = LLGetHead(&ts->stats) ; nextL ; nextL=nextL->next)
		{
			l = nextL;
		}
		
		l = LLGetHead(&ts->stats);  // l now points to the end of the LinkedList
		data = l->data;
		LLRemove(&ts->stats, data); // remove the link
		ts->numStats--;
	}

	// create new node for stats data, add it to the linked list, and fill in data
	tsd = amalloc(sizeof(struct TurfStatsData));
	LLAddFirst(&ts->stats, tsd);
	ts->numStats++;
	
	tsd->numFlags       = ta->numFlags;
	tsd->numPlayers     = ta->numPlayers;
	tsd->numTeams       = ta->numTeams;
	tsd->numWeights     = ta->numWeights;
	tsd->numPoints      = ta->numPoints;
	tsd->sumPerCapitas  = ta->sumPerCapitas;

	tsd->numTags        = ta->numTags;
	tsd->numLost        = ta->numLost;
	tsd->numRecovers    = ta->numRecovers;

	// copy freqs data
	LLInit(&tsd->freqs);
	for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
	{
		struct FreqInfo *src = l->data;
		struct FreqInfo *dst = amalloc(sizeof(struct FreqInfo));;

		dst->freq           = src->freq;
		dst->numFlags       = src->numFlags;
		dst->percentFlags   = src->percentFlags;
		dst->numWeights     = src->numWeights;
		dst->percentWeights = src->percentWeights;
		dst->numTags        = src->numTags;
		dst->numRecovers    = src->numRecovers;
		dst->numLost        = src->numLost;
		dst->numPlayers     = src->numPlayers;
		dst->perCapita      = src->perCapita;
		dst->percent        = src->percent;
		dst->numPoints      = src->numPoints;

		LLAdd(&tsd->freqs, dst);
	}

	ts->dingCount++;
	if (ts->dingCount >= ts->statsOnDing)
		ADisplay(arena, 0);  // output for the history we just copied

	UNLOCK_STATUS(arena);
}

/* arena should be locked already */
local void clearHistory(Arena *arena)
{
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l = NULL;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	for(l = LLGetHead(&ts->stats) ; l ; l=l->next)
	{
		struct TurfStatsData *data = l->data;;

		LLEnum(&data->freqs, afree);
		LLEmpty(&data->freqs);
	}

	LLEnum(&ts->stats, afree);
	LLEmpty(&ts->stats);
	ts->numStats = 0;
	ts->dingCount = 0;
}


/* arena should be locked already */
local void ADisplay(Arena *arena, int histNum)
{
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l;
	int x;
	struct TurfStatsData *tsd;
	struct FreqInfo *pFreq;

	if (!arena || !*p_ts) return; else ts = *p_ts;
	
	if( (histNum+1) > ts->numStats )
		return;  // history designated doesn't exist

	// get to the right link in ts->stats
	for(l = LLGetHead(&ts->stats), x=0 ; l ; l=l->next, x++)
	{
		if (x==histNum)
		{
			tsd = l->data;
			break;
		}
	}
	
	chat->SendArenaMessage(arena, "Freq\tPlyrs\tFlags\t%%Flgs\tWghts\t%%Wghts\tPerCap\t%%JP\tPts");
	chat->SendArenaMessage(arena, "----\t-----\t-----\t-----\t-----\t------\t------\t---\t---");
	
	// tsd now points to the stats we want to output, output freq stats
	for(l = LLGetHead(&tsd->freqs); l; l = l->next)
	{
		int freq, numFlags, percentFlags, numWeights, percentWeights, numTags, numRecovers, numLost, numPlayers, perCapita, percent, numPoints; 
		pFreq = l->data;

		// all the data members
		freq           = pFreq->freq;
		numFlags       = pFreq->numFlags;
		percentFlags   = pFreq->percentFlags;
		numWeights     = pFreq->numWeights;
		percentWeights = pFreq->percentWeights;
		numTags        = pFreq->numTags;
		numRecovers    = pFreq->numRecovers;
		numLost        = pFreq->numLost;
		numPlayers     = pFreq->numPlayers;
		perCapita      = pFreq->perCapita;
		percent        = pFreq->percent;
		numPoints      = pFreq->numPoints;

		if (numPoints > 0)
		{
			if (freq < 100)
			{
				// public freqs
				chat->SendArenaMessage(arena, 
					"%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
					freq, numPlayers, numFlags, percentFlags, numWeights, percentWeights, perCapita, percent, numPoints);
			}
			else
			{
				// private freqs
				chat->SendArenaMessage(arena, 
					"priv\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
					numPlayers, numFlags, percentFlags, numWeights, percentWeights, perCapita, percent, numPoints);
			}
		}
	}
}


/* arena should be locked already */
local void PDisplay(Arena *arena, Player *pid, int histNum)
{
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l;
	int x;
	struct TurfStatsData *tsd;
	struct FreqInfo *pFreq;

	if (!arena || !*p_ts) return; else ts = *p_ts;
	
	if( (histNum+1) > ts->numStats )
	{
		chat->SendMessage(pid, "History from %d dings ago is not available.", histNum);
		return;  // history designated doesn't exist
	}

	// get to the right link in ts->stats
	for(l = LLGetHead(&ts->stats), x=0 ; l ; l=l->next, x++)
	{
		if (x==histNum)
		{
			tsd = l->data;
			break;
		}
	}
	
	chat->SendMessage(pid, "Freq\tPlyrs\tFlags\t%%Flgs\tWghts\t%%Wghts\tPerCap\t%%JP\tPts");
	chat->SendMessage(pid, "----\t-----\t-----\t-----\t-----\t------\t------\t---\t---");
	
	// tsd now points to the stats we want to output, output freq stats
	for(l = LLGetHead(&tsd->freqs) ; l ; l=l->next)
	{
		int freq, numFlags, percentFlags, numWeights, percentWeights, numTags, numRecovers, numLost, numPlayers, perCapita, percent, numPoints; 
		pFreq = l->data;

		// all the data members
		freq           = pFreq->freq;
		numFlags       = pFreq->numFlags;
		percentFlags   = pFreq->percentFlags;
		numWeights     = pFreq->numWeights;
		percentWeights = pFreq->percentWeights;
		numTags        = pFreq->numTags;
		numRecovers    = pFreq->numRecovers;
		numLost        = pFreq->numLost;
		numPlayers     = pFreq->numPlayers;
		perCapita      = pFreq->perCapita;
		percent        = pFreq->percent;
		numPoints      = pFreq->numPoints;

		if (numPoints > 0)
		{
			if (freq < 100)
			{
				// public freqs
				chat->SendMessage(pid, 
					"%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
					freq, numPlayers, numFlags, percentFlags, numWeights, percentWeights, perCapita, percent, numPoints);
			}
			else
			{
				// private freqs
				chat->SendMessage(pid, 
					"priv\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d",
					numPlayers, numFlags, percentFlags, numWeights, percentWeights, perCapita, percent, numPoints);
			}
		}
	}
}

/*
local struct FreqInfo* findFreqPtr(Arena *arena, int histNum, int freq)
{
	struct FreqInfo *fiPtr;
	Link *l;
	
	LOCK_STATUS(arena);
	for(l = LLGetHead(&history[arena][histNum]->freqs) ; l ; l = l->next)
	{
		fiPtr = l->data;
		if(fiPtr->freq == freq)
		{
			UNLOCK_STATUS(arena);
			return fiPtr;   // found the freq
		}
	}
	UNLOCK_STATUS(arena);
	
	return NULL;  // freq didn't exist
}
*/

local helptext_t turfstats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Gets stats to previous dings.\n";
local void C_turfStats(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	
	// TODO: give more functionality using args to get history # so and so, right now only displays last ding
	int histNum = 0;
	
	LOCK_STATUS(arena);
	PDisplay(arena, p, histNum);
	UNLOCK_STATUS(arena);
}


local helptext_t forcestats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Displays stats to arena for previous dings.\n";
local void C_forceStats(const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);

	if (!arena || !*p_ts) return; else ts = *p_ts;

	// TODO: give more functionality using args to get history # so and so, right now only displays last ding
	int histNum = 0;
	
	LOCK_STATUS(arena);
	if( (histNum+1) > ts->numStats )
	{
		chat->SendMessage(p, "History from %d dings ago is not available.", histNum);
		return;  // history designated doesn't exist
	}
	ADisplay(arena, histNum);
	UNLOCK_STATUS(arena);
}

