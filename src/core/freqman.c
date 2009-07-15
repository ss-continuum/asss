
/* dist: public */

// TODO: integrate the old team size balance setting

#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "asss.h"

typedef struct
{
	int freq;
	int metric_sum;
	LinkedList players;
	int is_required;
} Freq;

typedef struct
{
	int metric;
	Freq *freq;
} pdata;

typedef struct
{
	LinkedList freqs;

	int include_spec;
	int max_freq;
	int priv_freq_start;
	int max_priv_size;
	int max_pub_size;
	int max_playing;
	int desired_teams;
	int max_x_res;
	int max_y_res;
	int initial_spec;
	int disallow_team_spectators;
} adata;

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *lm;
local Igame *game;

local int pdkey;
local int adkey;

/* protects the lists of Freqs and their player lists */
local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

/* checks the enforcers for allowable ships */
local int enforcers_get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	LinkedList advisers;
	Link *link;
	Aenforcer *adviser;
	int mask = 255;

	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->GetAllowableShips)
		{
			mask &= adviser->GetAllowableShips(p, ship, freq, err_buf, buf_len);

			if (mask == 0)
			{
				/* the player can't use any ships, might as well stop looping */
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return mask;
}

/* checks the enforcers for a freq change */
local int enforcers_can_change_freq(Arena *arena, Player *p, int new_freq, char *err_buf, int buf_len)
{
	Aenforcer *adviser;
	LinkedList advisers;
	Link *link;
	int can_change = 1;

	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->CanChangeFreq)
		{
			if (!adviser->CanChangeFreq(p, new_freq, err_buf, buf_len))
			{
				can_change = 0;
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return can_change;
}

/* query the balancer to get a new metric for a player */
local void balancer_update_metric(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	Ibalancer *balancer = mm->GetInterface(I_BALANCER, p->arena);
	if (balancer)
		data->metric = balancer->GetPlayerMetric(p);
	mm->ReleaseInterface(balancer);
}

/* query the balancer for the max metric for a freq */
local int balancer_get_max_metric(Arena *arena, int freq)
{
	Ibalancer *balancer = mm->GetInterface(I_BALANCER, arena);
	int val;
	if (balancer)
	{
		val = balancer->GetMaxMetric(arena, freq);
		mm->ReleaseInterface(balancer);
	}
	else
	{
		/* without a balancer, all metrics sum to 0 */
		val = 1;
	}

	return val;
}

local void update_freq(Player *p, int freq)
{
	pdata *data = PPDATA(p, pdkey);
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	Freq *i;
	Freq *new_freq = NULL;
	int oldfreqnum, newfreqnum, oldmetricnum, newmetricnum;

	LOCK();

	if (data->freq && data->freq->freq == freq)
	{
		lm->LogP(L_DRIVEL, "freqman", p, "Update_freq to same freq.");
		/* they're already on the correct freq,
		 * but update their metric while we're here */
		data->freq->metric_sum -= data->metric;
		balancer_update_metric(p);
		data->freq->metric_sum += data->metric;

		UNLOCK();
		return;
	}

	/* find their new freq */
	if (freq != -1)
	{
		FOR_EACH(&ad->freqs, i, link)
		{
			if (i->freq == freq)
			{
				new_freq = i;
				break;
			}
		}

		if (new_freq == NULL)
		{
			/* create a new freq for them */
			new_freq = amalloc(sizeof(*new_freq));
			new_freq->freq = freq;
			new_freq->metric_sum = 0;
			new_freq->is_required = (freq < ad->desired_teams);
			LLInit(&new_freq->players);
			LLAdd(&ad->freqs, new_freq);
		}
	}

	oldfreqnum = -1;
	oldmetricnum = -1;
	if (data->freq)
	{
		oldfreqnum = data->freq->freq;
		oldmetricnum = data->freq->metric_sum;
	}

	/* remove them from their old freq */
	if (data->freq)
	{
		data->freq->metric_sum -= data->metric;
		LLRemove(&data->freq->players, p);

		if (LLIsEmpty(&data->freq->players) && !data->freq->is_required)
		{
			lm->LogP(L_DRIVEL, "freqman", p, "Freeing freq %d with metric %d", data->freq->freq, data->freq->metric_sum);
			LLRemove(&ad->freqs, data->freq);
			afree(data->freq);
		}
	}

	/* get their new metric */
	balancer_update_metric(p);
	newmetricnum = -1;
	newfreqnum = -1;

	/* add them to their new freq */
	if (new_freq)
	{
		new_freq->metric_sum += data->metric;
		LLAdd(&new_freq->players, p);
		newfreqnum = new_freq->freq;
		newmetricnum = new_freq->metric_sum;
	}

	lm->LogP(L_DRIVEL, "freqman", p, "Old Freq=%d, New Freq=%d, Old Metric=%d, New Metric=%d", oldfreqnum, newfreqnum, oldmetricnum, newmetricnum);

	data->freq = new_freq;

	UNLOCK();
}

local Freq * get_freq(Arena *arena, int freq)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	Freq *f;

	LOCK();

	FOR_EACH(&ad->freqs, f, link)
	{
		if (f->freq == freq)
		{
			UNLOCK();
			return f;
		}
	}

	UNLOCK();

	return NULL;
}

/* count the playing players in the arena */
local int count_current_playing(Arena *arena)
{
	Player *p;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	int playing = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && p->arena == arena
				&& (p->p_ship != SHIP_SPEC || ad->include_spec)
				&& IS_HUMAN(p))
			playing++;
	pd->Unlock();
	return playing;
}

/* count the playing players on a freq */
local int count_freq(Arena *arena, int freq)
{
	Player *p;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	int t = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == arena && p->p_freq == freq
				&& (p->p_ship != SHIP_SPEC || ad->include_spec)
				&& IS_HUMAN(p))
			t++;
	pd->Unlock();
	return t;
}

local int is_freq_full(Arena *arena, int freq)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	int max;

	if (freq >= ad->priv_freq_start)
	{
		max = ad->max_priv_size;
	}
	else
	{
		max = ad->max_pub_size;
	}

	if (max == 0)
	{
		return 0;
	}
	else
	{
		return max <= count_freq(arena, freq);
	}
}

local int is_arena_full(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	int max = ad->max_playing;

	if (max == 0)
	{
		return 0;
	}
	else
	{
		return max <= count_current_playing(arena);
	}
}

local int screen_res_allowed(Player *p, char *err_buf, int buf_len)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	int allowed = 1;

	if (ad->max_x_res != 0 && p->xres > ad->max_x_res)
		allowed = 0;
	else if (ad->max_y_res != 0 && p->yres > ad->max_y_res)
		allowed = 0;

	if (!allowed && err_buf)
		snprintf(err_buf, buf_len, "Maximum allowed screen resolution is %dx%d in this arena", ad->max_x_res, ad->max_y_res);

	return allowed;
}

// TODO: this needs major changes
// I'd like to see is_required implemented to simplify the code for checking if a player can leave
// an empty pub freq.
local int can_change_freq(Arena *arena, Player *p, int new_freq_number, char *err_buf, int buf_len)
{
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(arena, adkey);
	int old_freq_number = p->p_freq;
	Freq *new_freq = get_freq(arena, new_freq_number);
	Freq *old_freq = get_freq(arena, old_freq_number);
	int new_freq_metric = data->metric;
	int old_freq_metric = 0;
	Ibalancer *balancer;

	/* check the enforcers first */
	if (!enforcers_can_change_freq(arena, p, new_freq_number, err_buf, buf_len))
		return 0;

	LOCK();

	/* update the player's metric, and their team's metric sum */
	if (old_freq)
	{
		old_freq->metric_sum -= data->metric;
		balancer_update_metric(p);
		old_freq->metric_sum += data->metric;
		old_freq_metric = old_freq->metric_sum;
	}
	else
	{
		balancer_update_metric(p);
	}

	/* compute what the new freq's metric will be after the player joins */
	if (new_freq)
	{
		new_freq_metric += new_freq->metric_sum;
	}

	/* check for various conditions to ensure desired teams are maintained */
	if (new_freq && new_freq->is_required && LLIsEmpty(&new_freq->players))
	{
		/* they're changing to an empty desired team: always allow */
		UNLOCK();
		return 1;
	}
	else if (!new_freq && new_freq_number < ad->desired_teams)
	{
		/* they're creating a desired team: always allow */
		UNLOCK();
		return 1;
	}
	else
	{
		/* check to see if they're emptying a desired team */
		if (old_freq && old_freq->is_required && LLCount(&old_freq->players) == 1)
		{
			/* they shouldn't be allowed to leave */
			snprintf(err_buf, buf_len, "Your frequency requires at least one player.");
			UNLOCK();
			return 0;
		}
		else
		{
			/* see if there are desired teams that need to be filled */
			Freq *i;
			Link *link;
			FOR_EACH(&ad->freqs, i, link)
			{
				if (i->is_required && LLIsEmpty(&i->players))
				{
					/* they shouldn't be allowed to leave */
					snprintf(err_buf, buf_len, "Frequency %d needs players first.", i->freq);
					UNLOCK();
					return 0;
				}
			}
		}
	}

	/* now check the balancer */
	balancer = mm->GetInterface(I_BALANCER, arena);
	if (balancer)
	{
		/* check the maximum metric requirement first */
		int max_metric = balancer->GetMaxMetric(arena, new_freq_number);
		if (max_metric && max_metric < new_freq_metric)
		{
			// FIXME: this needs better wording
			snprintf(err_buf, buf_len, "Changing to that freq would make them too good.");
			UNLOCK();
			return 0;
		}
		else
		{
			/* check the difference between the freqs */
			if (old_freq_metric && old_freq_number != arena->specfreq && balancer->GetMaximumDifference(arena, new_freq_number, old_freq_number) < new_freq_metric - old_freq_metric)
			{
				// FIXME: this needs better wording
				snprintf(err_buf, buf_len, "Changing to that freq would make the teams too uneven.");
				UNLOCK();
				return 0;
			}
			else
			{
				Freq *i;
				Link *link;

				/* iterate over all desired teams (not counting old and new) */
				FOR_EACH(&ad->freqs, i, link)
				{
					if (i != old_freq && i != new_freq && i->is_required)
					{
						/* check the new freq vs. i */
						if (balancer->GetMaximumDifference(arena, new_freq_number, i->freq) < new_freq_metric - i->metric_sum)
						{
							// FIXME: this needs better wording
							snprintf(err_buf, buf_len, "The players on freq %d wouldn't appreciate that...", i->freq);
							UNLOCK();
							return 0;
						}

						/* check the old freq vs. i */
						if (old_freq_metric && old_freq && old_freq->is_required && new_freq && new_freq->is_required)
						{
							if (balancer->GetMaximumDifference(arena, old_freq_number, i->freq) < i->metric_sum - old_freq_metric)
							{
								// FIXME: this needs better wording
								snprintf(err_buf, buf_len, "Your team needs you to fend off freq %d", i->freq);
								UNLOCK();
								return 0;
							}
						}
					}
				}
			}
		}
		mm->ReleaseInterface(balancer);
	}

	UNLOCK();
	return 1;
}

local int find_freq(Arena *arena, Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(arena, adkey);
	int i;
	Freq *best_freq = NULL;

	/* search only within the minimum of desired_teams and max_freq */
	int max = ad->desired_teams < ad->max_freq ? ad->desired_teams : ad->max_freq;

	for (i = 0; i < max; i++)
	{
		if (!is_freq_full(arena, i))
		{
			Freq *freq = get_freq(arena, i);
			if (!freq)
			{
				/* found an empty freq */
				return i;
			}
			else if (freq->metric_sum <= balancer_get_max_metric(arena, i) - data->metric)
			{
				if (!best_freq)
				{
					/* first freq we've found */
					best_freq = freq;
				}
				else
				{
					if (freq->metric_sum < best_freq->metric_sum)
					{
						best_freq = freq;
					}
				}
			}
		}
	}

	if (best_freq)
	{
		return best_freq->freq;
	}
	else
	{
		return arena->specfreq;
	}
}

local void Initial(Player *p, int *ship, int *freq)
{
	lm->LogP(L_DRIVEL, "freqman", p, "Entering Initial"); // FIXME: remove after debugging

	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	int f = *freq;
	int s = *ship;

	if (!arena) return;

	if (is_arena_full(arena) || p->flags.no_ship
			|| !screen_res_allowed(p, NULL, 0) || ad->initial_spec)
	{
		s = SHIP_SPEC;
	}

	balancer_update_metric(p);

	if (s == SHIP_SPEC)
	{
		/* don't need the enforcers since the player is going into spec */
		f = arena->specfreq;
	}
	else
	{
		int mask;

		/* find an initial freq using the balancer and enforcers*/
		f = find_freq(arena, p);

		if (f == arena->specfreq)
		{
			s = SHIP_SPEC;
		}
		else
		{
			/* make sure their ship is legal */
			mask = enforcers_get_allowable_ships(arena, p, s, f, NULL, 0);
			if ((mask & (1 << s)) == 0)
			{
				int i;

				s = SHIP_SPEC;

				for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
				{
					if (mask & (1 << i))
					{
						s = i;
						break;
					}
				}
			}

			/* if the enforcers didn't let them take a ship, send them to spec */
			if (s == SHIP_SPEC)
			{
				f = arena->specfreq;
			}
		}
	}

	*ship = s; *freq = f;

	update_freq(p, f);
}

local void ShipChange(Player *p, int requested_ship, char *err_buf, int buf_len)
{
	lm->LogP(L_DRIVEL, "freqman", p, "Entering ShipChange"); // FIXME: remove after debugging.

	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	int freq = p->p_freq;

	if (!arena) return;

	if (requested_ship >= SHIP_SPEC)
	{
		/* always allow switching to spec */
		game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
		return;
	}
	else if (p->flags.no_ship)
	{
		/* too much lag to get in a ship */
		if (err_buf)
			snprintf(err_buf, buf_len, "Too much lag to play in this arena.");
		return;
	}
	else if (!screen_res_allowed(p, err_buf, buf_len))
	{
		/* their resolution exceeds the arena limits */
		/* don't print to err_buf, screen_res_allowed takes care of it */
		return;
	}
	else if (p->p_ship == SHIP_SPEC && is_arena_full(arena))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people playing.");
		return;
	}

	/* make sure their ship is legal */
	if (requested_ship != SHIP_SPEC)
	{
		int mask = enforcers_get_allowable_ships(arena, p, requested_ship, freq, err_buf, buf_len);
		if ((mask & (1 << requested_ship)) == 0)
		{
			if (mask & (1 << p->p_ship))
			{
				/* default to the old ship */
				requested_ship = p->p_ship;
			}
			else
			{
				int i;
				requested_ship = SHIP_SPEC;
				for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
				{
					if (mask & (1 << i))
					{
						requested_ship = i;
						break;
					}
				}
			}
		}
	}

	/* they're coming out of specfreq, give 'em a new freq */
	if (freq == arena->specfreq && p->p_ship == SHIP_SPEC)
	{
		freq = find_freq(arena, p);

		if (freq == arena->specfreq)
		{
			requested_ship = SHIP_SPEC;
			if (err_buf)
				snprintf(err_buf, buf_len, "All public frequencies are full!");
		}
	}

	if (requested_ship == SHIP_SPEC && ad->disallow_team_spectators)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "Spectators are not allowed outside of the spectator frequency.");
		freq = arena->specfreq;
	}

	game->SetShipAndFreq(p, requested_ship, freq);

	/* update_freq is called in the shipfreqchange callback */
}

local void FreqChange(Player *p, int requested_freq, char *err_buf, int buf_len)
{
	lm->LogP(L_DRIVEL, "freqman", p, "Entering FreqChange"); // FIXME: remove after debugging

	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	int ship = p->p_ship;

	if (!arena) return;

	/* special case: speccer re-entering spec freq */
	if (ship == SHIP_SPEC && requested_freq == arena->specfreq)
	{
		game->SetFreq(p, arena->specfreq);
		return;
	}

	/* check if this change was from the specfreq, and if there are too
	 * many people playing. */
	if (ad->include_spec && ship == SHIP_SPEC && p->p_freq == arena->specfreq
			&& is_arena_full(arena))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people playing in this arena.");
		return;
	}

	if (requested_freq < 0 || requested_freq >= ad->max_freq)
	{
		/* they requested a bad freq. */
		if (ship == SHIP_SPEC && p->p_freq != arena->specfreq)
		{
			/* send him back to spec freq */
			game->SetFreq(p, arena->specfreq);
		}
		else
		{
			if (err_buf)
				snprintf(err_buf, buf_len, "Bad frequency.");
		}

		return;
	}
	else
	{
		if (!can_change_freq(arena, p, requested_freq, err_buf, buf_len))
		{
			/* balancer or enforcers won't let them change */
			return;
		}
	}

	/* check if there are too many on the freq */
	if ((ship != SHIP_SPEC || ad->include_spec)
			&& is_freq_full(arena, requested_freq))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people on that frequency.");
		return;
	}

	/* make sure their ship is legal */
	if (ship != SHIP_SPEC)
	{
		int mask = enforcers_get_allowable_ships(arena, p, ship, requested_freq, err_buf, buf_len);
		if (mask & (1 << ship))
		{
			game->SetFreq(p, requested_freq);
		}
		else
		{
			int new_ship = SHIP_SPEC;
			int i;
			for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
			{
				if (mask & (1 << i))
				{
					new_ship = i;
					break;
				}
			}

			if (new_ship == SHIP_SPEC && ad->disallow_team_spectators)
			{
				/* an error message is already provided by the enforcers */
				game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
			}
			else
			{
				game->SetShipAndFreq(p, new_ship, requested_freq);
			}
		}
	}
	else
	{
		if (ad->disallow_team_spectators)
		{
			if (err_buf)
				snprintf(err_buf, buf_len, "Spectators are not allowed outside of the spectator frequency.");
			game->SetFreq(p, arena->specfreq);
		}
		else
		{
			game->SetFreq(p, requested_freq);
		}
	}

	/* update_freq is called by the shipfreqchange callback */
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	update_freq(p, newfreq);
}

local void paction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey);

	if (action == PA_PREENTERARENA)
	{
		data->freq = NULL;
		data->metric = 0;
	}
	else if (action == PA_LEAVEARENA)
	{
		update_freq(p, -1);
	}
}

local void update_config(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	ConfigHandle ch = arena->cfg;

	/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
	 * Whether to include spectators when enforcing maximum freq sizes. */
	ad->include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

	/* cfghelp: Team:MaxFrequency, arena, int, range: 1-10000, def: 10000
	 * One more than the highest frequency allowed. Set this below
	 * PrivFreqStart to disallow private freqs. */
	ad->max_freq = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);

	/* cfghelp: Team:PrivFreqStart, arena, int, range: 0-9999, def: 100
	 * Freqs above this value are considered private freqs. */
	ad->priv_freq_start = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);

	/* cfghelp: Team:MaxPerPrivateTeam, arena, int, def: 0
	 * The maximum number of players on a private freq. Zero means
	 * no limit. */
	ad->max_priv_size = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 0);

	/* cfghelp: Team:MaxPerTeam, arena, int, def: 0
	 * The maximum number of players on a public freq. Zero means no
	 * limit. */
	ad->max_pub_size = cfg->GetInt(ch, "Team", "MaxPerTeam", 0);

	/* cfghelp: General:MaxPlaying, arena, int, def: 100
	 * This is the most players that will be allowed to play in the arena at
	 * once. Zero means no limit. */
	ad->max_playing = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/* cfghelp: Team:DesiredTeams, arena, int, def: 2
	 * The number of teams that the freq balancer will form as players
	 * enter. */
	ad->desired_teams = cfg->GetInt(arena->cfg, "Team", "DesiredTeams", 2);

	/* cfghelp: Misc:MaxXres, arena, int, def: 0
	 * Maximum screen width allowed in the arena. Zero means no limit. */
	ad->max_x_res = cfg->GetInt(ch, "Misc", "MaxXres", 0);

	/* cfghelp: Misc:MaxYres, arena, int, def: 0
	 * Maximum screen height allowed in the arena. Zero means no limit. */
	ad->max_y_res = cfg->GetInt(ch, "Misc", "MaxYres", 0);

	/* cfghelp: Team:InitialSpec, arena, bool, def: 0
	 * If players entering the arena are always assigned to spectator mode. */
	ad->initial_spec = cfg->GetInt(ch, "Team", "InitialSpec", 0);

	// FIXME: add cfghelp
	ad->disallow_team_spectators = cfg->GetInt(ch, "Team", "DisallowTeamSpectators", 0);
}

local void prune_freqs(Arena *arena)
{
	LinkedList to_free;
	Freq *freq;
	Link *link;
	int i;
	adata *ad = P_ARENA_DATA(arena, adkey);

	LLInit(&to_free);

	LOCK();

	/* make a list of frequencies to prune */
	FOR_EACH(&ad->freqs, freq, link)
	{
		if (freq->freq >= ad->desired_teams)
		{
			freq->is_required = 0;
			if (LLIsEmpty(&freq->players))
			{
				LLAdd(&to_free, freq);
			}
		}
	}
	/* and prune them */
	FOR_EACH(&to_free, freq, link)
	{
		LLRemove(&ad->freqs, freq);
		afree(freq);
	}

	/* make sure that the required teams exist */
	for (i = 0; i < ad->desired_teams; i++)
	{
		int found = 0;
		FOR_EACH(&ad->freqs, freq, link)
		{
			if (freq->freq == i)
			{
				freq->is_required = 1;
				found = 1;
				break;
			}
		}

		if (!found)
		{
			/* create the freq */
			Freq *new_freq = amalloc(sizeof(*new_freq));
			new_freq->freq = i;
			new_freq->metric_sum = 0;
			new_freq->is_required = 1;
			LLInit(&new_freq->players);
			LLAdd(&ad->freqs, new_freq);
		}
	}
	UNLOCK();
}

local void freq_free_enum(void *ptr)
{
	Freq *freq = ptr;
	// FIXME: remove this log entry after testing
	lm->Log(L_DRIVEL, "<freqman> freeing freq %d, metric=%d with %d players", freq->freq, freq->metric_sum, LLCount(&freq->players));
	LLEmpty(&freq->players);
	afree(freq);
}

local void aaction(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	if (action == AA_CREATE)
	{
		LLInit(&ad->freqs);
		update_config(arena);
		prune_freqs(arena);
	}
	else if (action == AA_DESTROY)
	{
		LOCK();
		LLEnumNC(&ad->freqs, freq_free_enum);
		LLEmpty(&ad->freqs);
		UNLOCK();
	}
	else if (action == AA_CONFCHANGED)
	{
		update_config(arena);
		prune_freqs(arena);
	}
}

local Ifreqman fm_int =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "freqman")
	Initial, ShipChange, FreqChange
};

EXPORT int MM_freqman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		if (!lm || !pd || !aman || !cfg || !game)
			return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (fm_int.head.refcount)
			return MM_FAIL;

		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);

		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		// TODO: init
		mm->RegInterface(&fm_int, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		// TODO: deinit
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregInterface(&fm_int, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

