
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
	int is_balanced_against;
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
	int required_teams;
	int max_x_res;
	int max_y_res;
	int max_res_area;
	int initial_spec;
	int disallow_team_spectators;
	int is_balanced_against_start;
	int is_balanced_against_end;
} adata;

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *lm;
local Igame *game;
local Imainloop *ml;

local int pdkey;
local int adkey;

/* protects the lists of Freqs and their player lists */
local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

/* checks the enforcers for allowable ships */
local shipmask_t enforcers_get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	LinkedList advisers;
	Link *link;
	Aenforcer *adviser;
	shipmask_t mask = SHIPMASK_ALL;

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
	if (IS_HUMAN(p))
	{
		Ibalancer *balancer = mm->GetInterface(I_BALANCER, p->arena);
		if (balancer)
		{
			data->metric = balancer->GetPlayerMetric(p);
		}
		else
		{
			data->metric = 0;
		}
		mm->ReleaseInterface(balancer);
	}
	else
	{
		data->metric = 0;
	}
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

local void sanity_check(Player *p, int freq)
{
#if 0 // HZ-only sanity testing for bug #79
	pdata *data = PPDATA(p, pdkey);
	if (data->freq)
	{
		Player *i;
		Link *link;
		int metric_sum = data->freq->metric_sum;
		int player_count = 0;
		pd->Lock();
		FOR_EACH_PLAYER(i)
			if (i->arena == p->arena && i->p_freq == data->freq->freq && IS_HUMAN(i))
			{
				player_count++;
			}
		pd->Unlock();
		if (metric_sum != player_count)
		{
			lm->LogP(L_DRIVEL, "freqman", p, "Failed metric check on freq %d (%d != %d, called on %d)", data->freq->freq, metric_sum, player_count, freq);
		}
	}
#endif
}

/* update the balancer metrics on a freq */
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
		/* they're already on the correct freq,
		 * but update their metric while we're here */
		data->freq->metric_sum -= data->metric;
		balancer_update_metric(p);
		data->freq->metric_sum += data->metric;
		sanity_check(p, freq);
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
			new_freq->is_required = (freq < ad->required_teams);
			new_freq->is_balanced_against = (ad->is_balanced_against_start <= freq && freq < ad->is_balanced_against_end);
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

	data->freq = new_freq;

	sanity_check(p, freq);
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

/* check if the team is full */
local int is_freq_full(Arena *arena, int freq)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Player *i;
	Link *link;
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
		int team_players = 0;
		pd->Lock();
		FOR_EACH_PLAYER(i)
			if (i->arena == arena && i->p_freq == freq
					&& (i->p_ship != SHIP_SPEC || ad->include_spec)
					&& IS_HUMAN(i))
			{
				team_players++;
			}
		pd->Unlock();

		return max <= team_players;
	}
}

/* check if the arena is full */
local int is_arena_full(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Player *i;
	Link *link;
	int playing;

	if (ad->max_playing == 0)
	{
		return 0;
	}
	else
	{
		playing = 0;
		pd->Lock();
		FOR_EACH_PLAYER(i)
			if (i->status == S_PLAYING && i->arena == arena
					&& i->p_ship != SHIP_SPEC
					&& IS_HUMAN(i))
			{
				playing++;
			}
		pd->Unlock();

		return ad->max_playing <= playing;
	}
}

/* check if a player's screen resolution is acceptable */
local int screen_res_allowed(Player *p, char *err_buf, int buf_len)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);

	if ((ad->max_x_res != 0 && p->xres > ad->max_x_res)
		|| (ad->max_y_res != 0 && p->yres > ad->max_y_res))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "Maximum allowed screen resolution is %dx%d in this arena", ad->max_x_res, ad->max_y_res);
		return 0;
	}
	else if (ad->max_res_area != 0 && p->xres * p->yres > ad->max_res_area)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "Maximum allowed screen area is %d in this arena", ad->max_res_area);
		return 0;
	}

	return 1;
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
		old_freq_metric = old_freq->metric_sum - data->metric;
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

	/* check for various conditions to ensure required teams are maintained */
	if (new_freq && new_freq->is_required && LLIsEmpty(&new_freq->players))
	{
		/* they're changing to an empty required team: always allow */
		UNLOCK();
		return 1;
	}
	else if (!new_freq && new_freq_number < ad->required_teams)
	{
		/* they're creating a required team: always allow */
		UNLOCK();
		return 1;
	}
	else
	{
		/* check to see if they're emptying a required team */
		if (old_freq && old_freq->is_required && LLCount(&old_freq->players) == 1)
		{
			/* they shouldn't be allowed to leave */
			snprintf(err_buf, buf_len, "Your frequency requires at least one player.");
			UNLOCK();
			return 0;
		}
		else
		{
			/* see if there are required teams that need to be filled */
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
			snprintf(err_buf, buf_len, "Changing to that frequency would make the teams too uneven.");
			UNLOCK();
			return 0;
		}
		else
		{
			/* check the difference between the freqs */
			if (old_freq_metric && old_freq_number != arena->specfreq && balancer->GetMaximumDifference(arena, new_freq_number, old_freq_number) < new_freq_metric - old_freq_metric)
			{
				snprintf(err_buf, buf_len, "Changing to that frequency would make the teams too uneven.");
				UNLOCK();
				return 0;
			}
			else
			{
				Freq *i;
				Link *link;

				/* iterate over all required teams (not counting old and new) */
				FOR_EACH(&ad->freqs, i, link)
				{
					if (i != old_freq && i != new_freq && i->is_balanced_against)
					{
						/* check the new freq vs. i */
						if (balancer->GetMaximumDifference(arena, new_freq_number, i->freq) < new_freq_metric - i->metric_sum)
						{
							snprintf(err_buf, buf_len, "Changing to that frequency would make the teams too uneven.");
							UNLOCK();
							return 0;
						}

						/* check the old freq vs. i */
						if (old_freq_metric && old_freq && old_freq->is_balanced_against && new_freq && new_freq->is_balanced_against)
						{
							if (balancer->GetMaximumDifference(arena, old_freq_number, i->freq) < i->metric_sum - old_freq_metric)
							{
								snprintf(err_buf, buf_len, "Changing to that frequency would make the teams too uneven.");
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

local int find_freq(Arena *arena, Player *p, char *err_buf, int buf_len)
{
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(arena, adkey);
	int i;
	Freq *best_freq = NULL;
	int max = ad->desired_teams;

	for (i = 0; i < max; i++)
	{
		if (!is_freq_full(arena, i))
		{
			if (enforcers_can_change_freq(arena, p, i, err_buf, buf_len))
			{
				Freq *freq = get_freq(arena, i);
				if (!freq)
				{
					/* found an empty freq */
					if (err_buf && buf_len)
					{
						/* clear any error messages */
						err_buf[0] = '\0';
					}
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
	}

	if (best_freq)
	{
		if (err_buf && buf_len)
		{
			/* clear any error messages */
			err_buf[0] = '\0';
		}
		return best_freq->freq;
	}
	else
	{
		/* couldn't find something on desired teams, search beyond */
		while (i < ad->max_freq)
		{
			if (!is_freq_full(arena, i))
			{
				Freq *freq = get_freq(arena, i);
				if (enforcers_can_change_freq(arena, p, i, err_buf, buf_len))
				{
					if (!freq)
					{
						/* empty freq */
						if (err_buf && buf_len)
						{
							/* clear any error messages */
							err_buf[0] = '\0';
						}
						return i;
					}
					else if (freq->metric_sum <= balancer_get_max_metric(arena, i) - data->metric)
					{
						/* has room */
						if (err_buf && buf_len)
						{
							/* clear any error messages */
							err_buf[0] = '\0';
						}
						return i;
					}
				}
				else
				{
					if (!freq)
					{
						/* enforcers rejected an empty freq, abort */
						break;
					}
				}
			}

			i++;
		}

		/* couldn't find anything beyond, return spec freq */
		return arena->specfreq;
	}
}

local void Initial(Player *p, int *ship, int *freq)
{
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
		shipmask_t mask;

		/* find an initial freq using the balancer and enforcers*/
		f = find_freq(arena, p, NULL, 0);

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
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	int freq = p->p_freq;

	if (!arena) return;

	/* setup the err_buf so we know if an enforcer wrote a message */
	if (err_buf && buf_len)
	{
		err_buf[0] = '\0';
	}

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

	/* they're coming out of specfreq, give 'em a new freq */
	if (freq == arena->specfreq && p->p_ship == SHIP_SPEC)
	{
		freq = find_freq(arena, p, err_buf, buf_len);

		if (freq == arena->specfreq)
		{
			requested_ship = SHIP_SPEC;
			if (err_buf)
				snprintf(err_buf, buf_len, "No frequencies are available!");
			return;
		}
	}
	else if (p->p_ship == SHIP_SPEC && !ad->include_spec && is_freq_full(arena, freq))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people on your frequency.");
		return;
	}

	/* make sure their ship is legal */
	if (requested_ship != SHIP_SPEC)
	{
		shipmask_t mask = enforcers_get_allowable_ships(arena, p, requested_ship, freq, err_buf, buf_len);
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

	if (requested_ship == SHIP_SPEC && ad->disallow_team_spectators)
	{
		if (err_buf && err_buf[0] == '\0')
			snprintf(err_buf, buf_len, "Spectators are not allowed outside of the spectator frequency.");
		freq = arena->specfreq;
	}

	game->SetShipAndFreq(p, requested_ship, freq);

	/* update_freq is called in the shipfreqchange callback */
}

local void FreqChange(Player *p, int requested_freq, char *err_buf, int buf_len)
{
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	int ship = p->p_ship;
	int already_checked_ship = 0;

	if (!arena) return;

	/* special case: speccer re-entering spec freq */
	if (ship == SHIP_SPEC && requested_freq == arena->specfreq)
	{
		game->SetFreq(p, arena->specfreq);
		return;
	}

	/* setup the err_buf so we know if an enforcer wrote a message */
	if (err_buf && buf_len)
	{
		err_buf[0] = '\0';
	}

	/* see if we need to put them into a ship */
	if (ad->disallow_team_spectators && ship == SHIP_SPEC)
	{

		if (p->flags.no_ship)
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

		if (IS_STANDARD(p))
		{
			shipmask_t mask = enforcers_get_allowable_ships(arena, p, SHIP_SPEC, requested_freq, err_buf, buf_len);
			int i;
			for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
			{
				if (mask & (1 << i))
				{
					ship = i;
					already_checked_ship = 1;
					break;
				}
			}
		}

		if (ship == SHIP_SPEC)
		{
			if (err_buf && *err_buf == '\0')
				snprintf(err_buf, buf_len, "Spectators are not allowed outside of the spectator frequency.");
			return;
		}
	}

	/* check if this change was from the spec, and if there are too
	 * many people playing. */
	if (p->p_ship == SHIP_SPEC && ship != SHIP_SPEC && is_arena_full(arena))
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

	/* check if there are too many on the freq */
	if ((ship != SHIP_SPEC || ad->include_spec)
			&& is_freq_full(arena, requested_freq))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people on that frequency.");
		return;
	}

	if (!can_change_freq(arena, p, requested_freq, err_buf, buf_len))
	{
			/* balancer or enforcers won't let them change */
			return;
	}

	/* make sure their ship is legal */
	if (ship != SHIP_SPEC && !already_checked_ship)
	{
		shipmask_t mask = enforcers_get_allowable_ships(arena, p, ship, requested_freq, err_buf, buf_len);
		if (mask & (1 << ship))
		{
			game->SetShipAndFreq(p, ship, requested_freq);
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
		game->SetShipAndFreq(p, ship, requested_freq);
	}

	/* update_freq is called by the shipfreqchange callback */
}

local int metric_update_timer(void *clos)
{
	Arena *arena = (Arena *)clos;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Ibalancer *balancer = mm->GetInterface(I_BALANCER, arena);
	Player *i;
	Link *link;
	Freq *freq;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		pdata *data = PPDATA(i, pdkey);
		if (!balancer || !IS_HUMAN(i))
		{
			data->metric = 0;
		}
		else
		{
			data->metric = balancer->GetPlayerMetric(i);
		}
	}
	pd->Unlock();
	mm->ReleaseInterface(balancer);

	LOCK();
	FOR_EACH(&ad->freqs, freq, link)
	{
		Link *player_link;
		freq->metric_sum = 0;
		FOR_EACH(&freq->players, i, player_link)
		{
			pdata *data = PPDATA(i, pdkey);
			freq->metric_sum += data->metric;
		}
	}
	UNLOCK();
	
	return TRUE;
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

	/* cfghelp: Team:RequiredTeams, arena, int, def: 0
	 * The number of teams that the freq manager will require to exist. */
	ad->required_teams = cfg->GetInt(arena->cfg, "Team", "RequriedTeams", 0);

	/* cfghelp: Team:BalancedAgainstStart, arena, int, def: 0
	 * Freqs >= BalancedAgainstStart and < BalancedAgainstEnd will be
	 * checked for balance even when players are not changing to or from
	 * these freqs. */
	ad->is_balanced_against_start = cfg->GetInt(arena->cfg, "Team", "BalancedAgainstStart", 0);

	/* cfghelp: Team:BalancedAgainstEnd, arena, int, def: 0
	 * Freqs >= BalancedAgainstStart and < BalancedAgainstEnd will be
	 * checked for balance even when players are not changing to or from
	 * these freqs. */
	ad->is_balanced_against_end = cfg->GetInt(arena->cfg, "Team", "BalancedAgainstEnd", 0);

	/* cfghelp: Misc:MaxXres, arena, int, def: 0
	 * Maximum screen width allowed in the arena. Zero means no limit. */
	ad->max_x_res = cfg->GetInt(ch, "Misc", "MaxXres", 0);

	/* cfghelp: Misc:MaxYres, arena, int, def: 0
	 * Maximum screen height allowed in the arena. Zero means no limit. */
	ad->max_y_res = cfg->GetInt(ch, "Misc", "MaxYres", 0);

	/* cfghelp: Misc:MaxResArea, arena, int, def: 0
	 * Maximum screen area (x*y) allowed in the arena, Zero means no limit. */
	ad->max_res_area = cfg->GetInt(ch, "Misc", "MaxResArea", 0);

	/* cfghelp: Team:InitialSpec, arena, bool, def: 0
	 * If players entering the arena are always assigned to spectator mode. */
	ad->initial_spec = cfg->GetInt(ch, "Team", "InitialSpec", 0);

	/* cfghelp: Team:DisallowTeamSpectators, arena, bool, def: 0
	 * If players are allowed to spectate outside of the spectator
	 * frequency. */
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
		if (freq->freq >= ad->required_teams)
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
	for (i = 0; i < ad->required_teams; i++)
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
	Freq *freq = (Freq*)ptr;
	LLEmpty(&freq->players);
	afree(freq);
}

local void aaction(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	if (action == AA_CREATE)
	{
		// TODO: init
		LLInit(&ad->freqs);
		update_config(arena);
		prune_freqs(arena);
		ml->SetTimer(metric_update_timer, 6000, 6000, arena, arena);
	}
	else if (action == AA_DESTROY)
	{
		// TODO: deinit
		LOCK();
		ml->ClearTimer(metric_update_timer, arena);
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
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!lm || !pd || !aman || !cfg || !game || !ml)
			return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, ALLARENAS);

		mm->RegInterface(&fm_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&fm_int, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, ALLARENAS);

		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
	return MM_FAIL;
}

