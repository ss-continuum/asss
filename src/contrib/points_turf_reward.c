/*
 * Defines the basic scoring algorithms for the turf_reward module
 */

#include "asss.h"
#include "turf_reward.h"
#include "settings/turfreward.h"  // bring in the settings for reward types


local Iplayerdata *playerdata;    // player data
local Ilogman     *logman;        // logging services
local Ichat       *chat;          // message players
local Iturfreward *turfreward;    // for multi arena processing (locking and unlocking)


/* function prototypes */
// connected to interface
// decides which of the basic scoring algorithms to use
local trstate_t calcReward(int arena, struct TurfArena *tr);

// reward calculation functions
local trstate_t crStandard(int arena, struct TurfArena *ta);  // REWARD_STD
local trstate_t crPeriodic(int arena, struct TurfArena *ta);  // REWARD_PERIODIC
local trstate_t crFixedPts(int arena, struct TurfArena *ta);  // REWARD_FIXED_PTS
local trstate_t crStdMulti(int arena, struct TurfArena *tr);  // REWARD_STD_MULTI


// to be registered with interface so that this module does scoring for turf_reward
local Iturfrewardpoints myint =
{
	INTERFACE_HEAD_INIT(I_TURFREWARD_POINTS, "trp-basic")
	calcReward
};


EXPORT const char info_points_turf_reward[] = "v1.1 by GiGaKiLLeR <gigamon@hotmail.com>";

EXPORT int MM_points_turf_reward(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		turfreward = mm->GetInterface(I_TURFREWARD, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(turfreward);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&myint, arena);   // register interface for certain arenas
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&myint, arena); // unregister interface for arenas that were using it
		return MM_OK;
	}
	return MM_FAIL;
}


local trstate_t calcReward(int arena, struct TurfArena *tr)
{
	switch(tr[arena].reward_style)
	{
	case REWARD_PERIODIC:  return crPeriodic(arena, &tr[arena]);
	case REWARD_FIXED_PTS: return crFixedPts(arena, &tr[arena]);  // TODO: havn't decided how to specify points
	case REWARD_STD:       return crStandard(arena, &tr[arena]);
	case REWARD_STD_MULTI: return crStdMulti(arena, tr);  // TODO: super hard, last thing on todo list
	case REWARD_DISABLED:  return TR_NO_AWARD_NO_UPDATE;
	}
	return TR_FAIL_CALCULATIONS;  // unknown reward style, failed calculations, no reward or update
	                              // it's possible that someone forgot to take this module out of loading
	                              // from the conf when they wrote their own custom reward module
}


local trstate_t crStandard(int arena, struct TurfArena *ta)
{
	//int x;
	//struct TurfFlag *flags = ta->flags;
	struct FreqInfo *pFreq = NULL;
	LinkedList getPts;  // linked list of freqs that will recieve points
	Link *l;

	// make sure these are clear (they should be already)
	ta->numPlayers    = 0;
	ta->numPoints     = 0;
	ta->numTeams      = 0;
	ta->numWeights    = 0;
	ta->sumPerCapitas = 0;

	// make sure cfg settings are valid (we dont want any crashes in here)
	if(ta->min_players_on_freq < 1)
		ta->min_players_on_freq = 1;
	if(ta->min_flags < 1)
		ta->min_flags = 1;
	if(ta->min_weights < 1)
		ta->min_weights = 1;

	// make sure freq info for each freq is initalized
	for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
	{
		pFreq = l->data;
		pFreq->percent   = 0;
		pFreq->numPoints = 0;
	}

	/* # flags, # weights, and # of players for every freq (and thus the entire arena) is already recorded for us
	 * in order for us to figure out % flags we must be sure # Flags in arena > 0
	 * so lets make sure that map does in fact have flags to score for (numFlags for arena > 0) */
	if (ta->numFlags < 1)
	{
		// no flags, therefore no weights, stop right here
		logman->LogA(L_WARN, "points_turf_reward", arena, "Map has no flags.");
		return TR_FAIL_CALCULATIONS;
	}

	// in order for us to figure out % weights we must be sure that numWeights for arena > 0
	if (ta->numWeights < 1)
	{
		// no team owns flags
		logman->LogA(L_DRIVEL, "points_turf_reward", arena, "No one owns any weights.");
		chat->SendArenaMessage(arena, "Notice: all flags are unowned.");
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Reward:0 (arena minimum requirements not met)");
		return TR_FAIL_REQUIREMENTS;
	}

	// fill in % flags and % weights
	for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
	{
		pFreq = l->data;
		if ( (pFreq->numFlags>=0) && (pFreq->numWeights>=0) )
		{
			pFreq->percentFlags = ((double)pFreq->numFlags) / ((double)ta->numFlags) * 100.0;
			pFreq->percentWeights = ((double)pFreq->numWeights) / ((double)ta->numWeights) * 100.0;
		}
		else
		{
			// negative numFlags or numWeights? wtf?
			pFreq->percentFlags = 0;
			pFreq->percentWeights = 0;
		}
	}

	/* # flags, %flags, # weights, % weights, and # of players known for every freq
	 * and # flags, # weights, and # players for the arena */

	// now that we know how many players there are, check if there are enough players for rewards
	if ( ta->numPlayers < ta->min_players_in_arena )
	{
		logman->LogA(L_DRIVEL, "points_turf_reward", arena,
			"Not enough players in arena for rewards.  Current:%i Minimum:%i",
			ta->numPlayers,
			ta->min_players_in_arena);
		chat->SendArenaMessage(arena, "Notice: not enough players for rewards.");
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Reward:0 (arena minimum requirements not met)");
		return TR_FAIL_REQUIREMENTS;
	}

	// count how many valid teams exist (valid meaning enough players to be considered a team)
	for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
	{
		pFreq = l->data;
		if ( pFreq->numPlayers >= ta->min_players_on_freq )
			ta->numTeams++;
	}

	/* at this point # flags, %flags, # weights, % weights, and # of players for every freq
	 * and # flags, # weights, # players, and # teams for arena are recorded */

	// now that we know how many teams there are, check if there are enough teams for rewards
	if (ta->numTeams < ta->min_teams)
	{
		logman->LogA(L_DRIVEL, "points_turf_reward", arena, 
			"Not enough teams in arena for rewards.  Current:%i Minimum:%i",
			ta->numTeams,
			ta->min_teams);
		chat->SendArenaMessage(arena, "Notice: not enough teams for rewards.");
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Reward:0 (arena minimum requirements not met)");
		return TR_FAIL_REQUIREMENTS;
	}

	LLInit(&getPts);
	for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
	{
		pFreq = l->data;
		
		if (pFreq->numPlayers > 0)  // fill in percapita for all teams with players
			pFreq->perCapita = ((double)pFreq->numWeights) / ((double)pFreq->numPlayers);
			
		if (       (pFreq->numPlayers     >= ta->min_players_on_freq)
			&& (pFreq->numFlags       >= ta->min_flags)
			&& (pFreq->numWeights     >= ta->min_weights)
			&& (pFreq->percentFlags   >= ta->min_percent_flags)
			&& (pFreq->percentWeights >= ta->min_percent_weights) )
		{
			// sum up the percapitas for freqs that PASSED minimum requirements
			ta->sumPerCapitas+=pFreq->perCapita;
			
			LLAddFirst(&getPts, pFreq);  // freq passed reward requirements
		}
		//else
			//LLAddFirst(&noPts, pFreq);   // freq failed reward requirements
	}

	/* # flags, %flags, # weights, % weights, # of players, and perCapita is set for every freq
	 * and # flags, # weights, # players, # teams, and sumPerCapita for arena are recorded */

	// figure out percent of jackpot team will recieve and how many points that relates to
	ta->numPoints = ta->jackpot_modifier * ta->numPlayers;

	if(LLIsEmpty(&getPts) || ta->sumPerCapitas<1)
	{
		// no team passed min requirements, no one gets points, however, arena requirements passed
		for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
		{
			pFreq = l->data;

			pFreq->percent   = 0;
			pFreq->numPoints = 0;
		}
		
		// tell everyone they got no points
		chat->SendArenaMessage(arena, "Notice: no teams met reward requirements but arena requirements were OK.");
	}
	else
	{
		// at least one team passed minimum requirements, award them points
		for(l = LLGetHead(&getPts) ; l ; l=l->next)
		{
			pFreq = l->data;
			pFreq->percent = (double)(pFreq->perCapita / ta->sumPerCapitas * 100.0);
			if(pFreq->numPlayers>0)  // double check, min_players_on_freq should have already weeded any out
				pFreq->numPoints = (int)(ta->numPoints * (pFreq->percent/100) / pFreq->numPlayers);
			else
			{
				// this should never ever happen
				logman->LogA(L_WARN, "points_turf_reward", arena, "When calculating numPoints, a team that passed min requirements had 0 players. Check that min_players_freq > 0.");
			}
		}
	}
	
	LLEmpty(&getPts);  // empty linked list, but DO NOT free the memory of the nodes up because they are also attached to freqs
	return TR_AWARD_UPDATE;
}


local trstate_t crPeriodic(int arena, struct TurfArena *ta)
{
	int modifier = ta->jackpot_modifier;
	struct FreqInfo *pFreq;
	Link *l;

	crStandard(arena, ta);  // cheap way of doing it
	                        // TODO: change to do it's own dirty work

	if(modifier == 0)  // means dings disabled
	{
		// no one gets points
		for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
		{
			pFreq = l->data;
			
			pFreq->percent   = 0;
			pFreq->numPoints = 0;
		}
		return TR_UPDATE_ONLY;
	}
	
	if(modifier > 0)
	{
		// # points = modifier * # flags owned
		for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
		{
			pFreq = l->data;
			
			pFreq->percent   = 0;
			pFreq->numPoints = modifier * pFreq->numFlags;
		}
	}
	else
	{
		// # points = modifier * # flags owned * # players in arena
		int numPlayers = ta->numPlayers;

		for(l = LLGetHead(&ta->freqs) ; l ; l=l->next)
		{
			pFreq = l->data;
			
			pFreq->percent   = 0;
			pFreq->numPoints = numPlayers * (-modifier) * pFreq->numFlags;
		}
	}
	return TR_AWARD_UPDATE;
}


local trstate_t crFixedPts(int arena, struct TurfArena *ta)
{
	crStandard(arena, ta);
	
	// TODO
	// now figure out which freq is 1st place, 2nd place, 3rd place...
	
	return TR_FAIL_CALCULATIONS;
}


// FIXME: for multiple arenas there can must only be 1 timer, turf_reward.c is going to need a couple modifications
//        before multiple arena scoring can be handled
local trstate_t crStdMulti(int arena, struct TurfArena *tr)
{
	// TODO
	/*
	int x;
	int id = tr[arena].multi_arena_id;
	struct TurfArena *arenaPtr;
	LinkedList *arenaLL;
	Link *l;
	
	LLInit(&arenaLL);
	
	LLAdd(&arenaLL, tr[arena]); // add the arena that called the timer
	
	// figure out what arenas are included in the calculations
	for(x=0 ; x<MAXARENA ; x++)
	{
		if(x==arena)
			continue;
		turfreward->LockTurfStatus(x);
		if (tr[x] && tr[x].flags && (tr[x].reward_style==REWARD_STD_MULTI) && (tr[x].multi_arena_id==id))
			LLAdd(&arenaLL, tr[x])
		else
			turfreward->UnlockTurfStatus(x);  // only unlock arenas that are not part of this
	}
	
	LLEmpty(&arenaLL);
	*/
	return TR_FAIL_CALCULATIONS;
}


