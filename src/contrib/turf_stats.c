
/* CURRENTLY BROKEN */

/*
 * Turf Statistics Module for ASSS
 * - gathers records of data from turf_reward
 */


// TODO: 1.  extend module to handle indiviual player stats, right now it only does team stats
//       2.  extend module to handle commands with targets (for example,  a command to get the
//           server to output you team's stats to your team chat)
//       3.  convert turf_stats to use it's own special data structure instead of reusing  
//           TurfArena structs.  that way we only store what is necessary and useful to stats

#include "asss.h"
#include "turf_reward.h"

#define MAXHISTORY 10  // maximum historical data of previous rewards to keep

local Imodman     *mm;
local Iplayerdata *playerdata;          // player data
local Iarenaman   *arenaman;            // arena manager
local Iconfig     *config;              // config (for arena .cfg) services
local Ilogman     *logman;              // logging services
local Ichat       *chat;                // message players
local Icmdman     *cmdman;              // for command handling

local struct TurfArena *history[MAXARENA][MAXHISTORY]; // where history will be stored
local pthread_mutex_t mtx[MAXARENA];                   // to keep things thread safe when accessing tr

/* function prototypes */
// connected to callbacks
local void postReward(int arena, struct TurfArena *ta);

/* comands */
local helptext_t turfstats_help, forcestats_help;
// standard user commands
local void C_turfStats(const char *, int, const Target *);    // to get stats for all freqs
// mod commands
local void C_forceStats(const char *, int, const Target *);   // to force stats to be outputted on the spot


// helper functions
local void ADisplay(int arena, int histNum);
local void PDisplay(int arena, int pid, int histNum);
local void removeHistory(int arena, int histNum);
local struct FreqInfo* findFreqPtr(int arena, int histNum, int freq);


EXPORT const char info_turf_stats[] = "v1.1 by GiGaKiLLeR <gigamon@hotmail.com>";

EXPORT int MM_turf_stats(int action, Imodman *_mm, int arena)
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

		cmdman->AddCommand("turfstats", C_turfStats, turfstats_help);
		cmdman->AddCommand("forcestats", C_forceStats, forcestats_help);
		
		// initialize the mutual exclusions
		{
			int x;
			pthread_mutexattr_t attr;
			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

			for (x=0; x < MAXARENA; x++)
				pthread_mutex_init(mtx + x, &attr);
			pthread_mutexattr_destroy(&attr);
		}

		// init history array
		{
			int x;
			int y;
			for(x=0 ; x<MAXARENA ; x++)
				for(y=0 ; y<MAXHISTORY ; y++)
					history[x][y]=NULL;
		}
		
		return MM_OK;
	}
	else if (action == MM_UNLOAD)  // when the module is to be unloaded
	{
		cmdman->RemoveCommand("turfstats", C_turfStats);
		cmdman->RemoveCommand("forcestats", C_forceStats);
		
		// release all interfaces
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(arenaman);
		mm->ReleaseInterface(config);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmdman);

		// free history array
		{
			int x;
			int y;
			for(x=0 ; x<MAXARENA ; x++)
				for(y=0 ; y<MAXHISTORY ; y++)
					if (history[x][y])
						removeHistory(x,y);
		}
		return MM_OK;
	}
	else if (action == MM_ATTACH)  // module only attached to an arena if listed in conf
	{
		// create all necessary callbacks
		mm->RegCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		// unregister all the callbacks
		mm->UnregCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


local void postReward(int arena, struct TurfArena *ta)
{
	int x;
	struct TurfArena *newTA = amalloc(sizeof(struct TurfArena));
	
	LOCK_STATUS(arena);
	
	if (history[arena][MAXHISTORY-1])                   // if we already have the maximum # of histories
		removeHistory(arena,MAXHISTORY-1);          // get rid of the oldest history
	
	for(x=MAXHISTORY-1 ; x>0 ; x--)
		history[arena][x] = history[arena][x-1];    // move any previous reward histories back one
	
	history[arena][0] = newTA;                          // reward now becomes most recent history

	// copy all of the data from ta into newTA
	*newTA = *ta;
	newTA->flags   = NULL;
	newTA->weights = NULL;
	newTA->trp     = NULL;
	/* FIXME: not sure what do do about the linked list for freqs */
	
	/*
	newTA->reward_style         = ta->reward_style;
	newTA->min_players_on_freq  = ta->min_players_on_freq;
	newTA->min_players_in_arena = ta->min_players_in_arena;
	newTA->min_teams            = ta->min_teams;
	newTA->min_flags            = ta->min_flags;
	newTA->min_percent_flags    = ta->min_percent_flags;
	newTA->min_weights          = ta->min_weights;
	newTA->min_percent_weights  = ta->min_percent_weights;
	newTA->min_percent          = ta->min_percent;
	newTA->jackpot_modifier     = ta->jackpot_modifier;
	newTA->recover_dings        = ta->recover_dings;
	newTA->arena                = ta->arena;
	newTA->timer_initial        = ta->timer_initial;
	newTA->timer_interval       = ta->timer_interval;
	newTA->trp                  = NULL;
	newTA->numFlags             = ta->numFlags;
	newTA->numPlayers           = ta->numPlayers;
	newTA->numTeams             = ta->numTeams;
	newTA->numWeights           = ta->numWeights;
	newTA->numPoints            = ta->numPoints;
	newTA->sumPerCapitas        = ta->sumPerCapitas;
	*/
	
	// copy flag data
	{
		struct TurfFlag *flags  = amalloc(newTA->numFlags * sizeof(struct TurfFlag));	
		newTA->flags = flags;
		for(x=0 ; x<newTA->numFlags ; x++)
		{
			Link *l;
			struct OldNode *source;
			struct OldNode *dest;
			
			flags[x].freq      = ta->flags[x].freq;
			flags[x].dings     = ta->flags[x].dings;
			flags[x].weight    = ta->flags[x].weight;
			flags[x].taggerPID = ta->flags[x].taggerPID;

			// copy linked list of freqs that can recover
			LLInit(&flags[x].old);
			for(l = LLGetHead(&ta->flags[x].old); l; l = l->next)
			{
				source          = l->data; 
				dest            = amalloc(sizeof(struct OldNode));
				
				dest->lastOwned = source->lastOwned;
				dest->freq      = source->freq;
				dest->dings     = source->dings;
				dest->weight    = source->weight;
				dest->taggerPID = source->taggerPID;
				
				LLAdd(&flags[x].old, dest);
			}
		}
	}
	
	// copy freq data
	{
		struct FreqInfo *src;
		struct FreqInfo *dst;
		Link *l;
		
		LLInit(&newTA->freqs);
		for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
		{
			src = l->data;
			dst = amalloc(sizeof(struct FreqInfo));

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
		}
	}
	
	ADisplay(arena, 0);  // output for the history we just copied
	UNLOCK_STATUS(arena);
}


/* arena should be locked already */
local void ADisplay(int arena, int histNum)
{
	Link *l;
	struct TurfArena *ta;
	//struct TurfFlag *flags;
	struct FreqInfo *pFreq;
	
	if(!history[arena][histNum])
	{
		UNLOCK_STATUS(arena);
		return;  // history designated doesn't exist
	}
	
	ta    = history[arena][histNum];
	//flags = &ta->flags;
	
	chat->SendArenaMessage(arena, "Freq\tPlyrs\tFlags\t%%Flgs\tWghts\t%%Wghts\tPerCap\t%%JP\tPts");
	chat->SendArenaMessage(arena, "----\t-----\t-----\t-----\t-----\t------\t------\t---\t---");
	
	//Link *l;
	for(l = LLGetHead(&ta->freqs); l; l = l->next)
	{
		int freq, numFlags, percentFlags, numWeights, percentWeights, numTags, numRecovers, numLost, numPlayers, perCapita, percent, numPoints; 
		pFreq = l->data;

		// all the data members
		// TODO: make output look nice
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
local void PDisplay(int arena, int pid, int histNum)
{
	Link *l;
	struct TurfArena *ta;
	//struct TurfFlag *flags;
	struct FreqInfo *pFreq;
	
	if(!history[arena][histNum])
	{
		UNLOCK_STATUS(arena);
		return;  // history designated doesn't exist
	}
	
	ta    = history[arena][histNum];
	//flags = &ta->flags;
	
	chat->SendMessage(pid, "Freq\tPlyrs\tFlags\t%%Flgs\tWghts\t%%Wghts\tPerCap\t%%JP\tPts");
	chat->SendMessage(pid, "----\t-----\t-----\t-----\t-----\t------\t------\t---\t---");
	
	//Link *l;
	for(l = LLGetHead(&ta->freqs); l; l = l->next)
	{
		int freq, numFlags, percentFlags, numWeights, percentWeights, numTags, numRecovers, numLost, numPlayers, perCapita, percent, numPoints; 
		pFreq = l->data;

		// all the data members
		// TODO: make output look nice
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


/* arena should be locked already */
void removeHistory(int arena, int histNum)
{	
	if (!history[arena][histNum]) return;  // history was already removed
	
	// remove the flags portion
	if (history[arena][histNum]->flags)
	{
		int z;

		// remove the linked list of previous owners
		for(z=0 ; z<history[arena][histNum]->numFlags ; z++)
		{
			LLEnum(&history[arena][histNum]->flags[z].old, afree);
			LLEmpty(&history[arena][histNum]->flags[z].old);
		}
		
		// now get rid of the flags array
		afree(history[arena][histNum]->flags);
		history[arena][histNum]->flags = NULL;
	}
	
	// remove the weights array
	if(history[arena][histNum]->weights)
	{
		afree(history[arena][histNum]->weights);
		history[arena][histNum]->weights = NULL;
	}
	
	// remove the freqs linked list
	LLEnum(&history[arena][histNum]->freqs, afree);
	LLEmpty(&history[arena][histNum]->freqs);

	// remove the array of TurfArena structs
	afree(history[arena][histNum]);
	history[arena][histNum] = NULL;
}


local struct FreqInfo* findFreqPtr(int arena, int histNum, int freq)
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


local helptext_t turfstats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Gets stats to previous dings.\n";
local void C_turfStats(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	
	// TODO: give more functionality using args to get history # so and so, right now only displays last ding
	int histNum = 0;
	
	if (ARENA_BAD(arena)) return;
	
	LOCK_STATUS(arena);
	if (histNum>MAXHISTORY-1 || !history[arena][histNum])
	{
		chat->SendMessage(pid, "History from %d dings ago is not available.", histNum);
		UNLOCK_STATUS(arena);
		return;
	}
	PDisplay(arena, pid, histNum);
	UNLOCK_STATUS(arena);
}


local helptext_t forcestats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Displays stats to arena for previous dings.\n";
local void C_forceStats(const char *params, int pid, const Target *target)
{
	int arena = playerdata->players[pid].arena;
	
	// TODO: give more functionality using args to get history # so and so, right now only displays last ding
	int histNum = 0;
	
	if (ARENA_BAD(arena)) return;
	
	LOCK_STATUS(arena);
	if (histNum>MAXHISTORY-1 || !history[arena][histNum])
	{
		chat->SendMessage(pid, "History from %d dings ago is not available.", histNum);
		UNLOCK_STATUS(arena);
		return;
	}
	ADisplay(arena, histNum);
	UNLOCK_STATUS(arena);
}

