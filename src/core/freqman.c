
/* dist: public */

// TODO: make sure the cfghelps are present once and only once

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
} Freq;

typedef struct
{
	int metric;
	Freq *freq;
} pdata;

typedef struct
{
	LinkedList freqs;
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

local void update_metric(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	Ibalancer *balancer = mm->GetInterface(I_BALANCER, p->arena);
	if (balancer)
		data->metric = balancer->GetPlayerMetric(p);
	mm->ReleaseInterface(balancer);
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
		/* they're already on the correct frestq,
		 * but update their metric while we're here */
		data->freq->metric_sum -= data->metric;
		update_metric(p);
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

		if (LLIsEmpty(&data->freq->players))
		{
			lm->LogP(L_DRIVEL, "freqman", p, "Freeing freq %d with metric %d", data->freq->freq, data->freq->metric_sum);
			LLRemove(&ad->freqs, data->freq);
			afree(data->freq);
		}
	}

	/* get their new metric */
	update_metric(p);
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

local int count_current_playing(Arena *arena, int include_spec)
{
	Player *p;
	Link *link;
	int playing = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && p->arena == arena
				&& (p->p_ship != SHIP_SPEC || include_spec)
				&& IS_HUMAN(p))
			playing++;
	pd->Unlock();
	return playing;
}

local int count_freq(Arena *arena, int freq, Player *excl, int include_spec)
{
	Player *p;
	Link *link;
	int t = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == arena && p->p_freq == freq && p != excl
				&& (p->p_ship != SHIP_SPEC || include_spec)
				&& IS_HUMAN(p))
			t++;
	pd->Unlock();
	return t;
}

local int is_freq_full(Arena *arena, ConfigHandle ch, int freq, int include_spec)
{
	int max;

	/* cfghelp: Team:PrivFreqStart, arena, int, range: 0-9999, def: 100
	 * Freqs above this value are considered private freqs. */
	if (freq >= cfg->GetInt(ch, "Team", "PrivFreqStart", 100))
	{
		/* cfghelp: Team:MaxPerPrivateTeam, arena, int, def: 0
		 * The maximum number of players on a private freq. Zero means
		 * no limit. */
		max = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 0);
	}
	else
	{
		/* cfghelp: Team:MaxPerTeam, arena, int, def: 0
		 * The maximum number of players on a public freq. Zero means no
		 * limit. */
		max = cfg->GetInt(ch, "Team", "MaxPerTeam", 0);
	}

	if (max == 0)
		return 0;

	return max <= count_freq(arena, freq, NULL, include_spec);
}

local int screen_res_allowed(Player *p, ConfigHandle ch, char *err_buf, int buf_len)
{
	/* cfghelp: Misc:MaxXres, arena, int, def: 0
	 * Maximum screen width allowed in the arena. Zero means no limit. */
	int max_x = cfg->GetInt(ch, "Misc", "MaxXres", 0);
	/* cfghelp: Misc:MaxYres, arena, int, def: 0
	 * Maximum screen height allowed in the arena. Zero means no limit. */
	int max_y = cfg->GetInt(ch, "Misc", "MaxYres", 0);

	if((max_x == 0 || p->xres <= max_x) && (max_y == 0 || p->yres <= max_y))
		return 1;

	if (err_buf)
		snprintf(err_buf, buf_len, "Maximum allowed screen resolution is %dx%d in this arena", max_x, max_y);

	return 0;
}

local int get_max_metric(Arena *arena, int freq)
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


local int can_change_freq(Arena *arena, Player *p, int new_freq, char *err_buf, int buf_len)
{
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(arena, adkey);
	Aenforcer *adviser;
	LinkedList advisers;
	Link *link;
	int can_change = 1;
	int old_freq = p->p_freq;

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

	if (can_change)
	{
		int desired_teams = cfg->GetInt(arena->cfg, "Team", "DesiredTeams", 2);
		Freq *freq = get_freq(arena, new_freq);
		Freq *old = get_freq(arena, old_freq);
		int max_metric;
		int new_freq_metric = data->metric;
		int old_freq_metric = 0;

		LOCK();

		/* update the player's metric */
		if (old)
		{
			old->metric_sum -= data->metric;
			update_metric(p);
			old->metric_sum += data->metric;
		}
		else
		{
			update_metric(p);
		}

		if (freq)
		{
			new_freq_metric += freq->metric_sum;
		}

		if (old && old_freq < desired_teams && old_freq != arena->specfreq)
		{
			old_freq_metric = old->metric_sum - data->metric;

			/* check to make sure they're not emptying one of the desired teams */
			if (LLCount(&old->players) == 1)
			{
				// FIXME: needs better wording
				snprintf(err_buf, buf_len, "Your frequency requires at least one player.");
				can_change = 0;
			}
		}

		// TODO: check for desired teams

		/* do a special check to see if they're switching to the lowest pub team from spectator */
		if (can_change && old_freq == arena->specfreq && new_freq < desired_teams)
		{
			if (freq)
			{
				int found = 0;
				Freq *i;
				FOR_EACH(&ad->freqs, i, link)
				{
					if (i->freq < desired_teams && i->metric_sum < freq->metric_sum)
					{
						found = 1;
						break;
					}
				}

				if (!found)
				{
					/* any player can always switch to the lowest desired team */
					/* this may throw off balance, but it should always be allowed */
					UNLOCK();
					return 1;
				}
			}
			else
			{
				/* any player can always switch to an empty desired team */
				UNLOCK();
				return 1;
			}
		}

		if (can_change)
		{
			Ibalancer *balancer = mm->GetInterface(I_BALANCER, arena);
			if (balancer)
			{


				/* check the max metric */
				max_metric = balancer->GetMaxMetric(arena, new_freq);
				if (freq && max_metric && max_metric < new_freq_metric)
				{
					// FIXME: this needs better wording
					snprintf(err_buf, buf_len, "Changing to that freq would make them too good.");
					can_change = 0;
				}
				else
				{
					if (old_freq_metric && balancer->GetMaximumDifference(arena, new_freq, old_freq) < new_freq_metric - old_freq_metric)
					{
						// FIXME: this needs better wording
						snprintf(err_buf, buf_len, "Changing to that freq would make the teams too uneven.");
						can_change = 0;
					}
					else
					{
						Freq *i;

						/* iterate over all pub freqs (not counting old and new) */
						FOR_EACH(&ad->freqs, i, link)
						{
							if (i != old && i != freq && i->freq < desired_teams)
							{
								/* check the new freq vs. i */
								if (balancer->GetMaximumDifference(arena, new_freq, i->freq) < new_freq_metric - i->metric_sum)
								{
									// FIXME: this needs better wording
									snprintf(err_buf, buf_len, "The players on freq %d wouldn't appreciate that...", i->freq);
									can_change = 0;
									break;
								}

								if (old_freq_metric && new_freq < desired_teams)
								{
									if (balancer->GetMaximumDifference(arena, old_freq, i->freq) < i->metric_sum - old_freq_metric)
									{
										// FIXME: this needs better wording
										snprintf(err_buf, buf_len, "Your team needs you to fend off freq %d", i->freq);
										can_change = 0;
										break;
									}
								}
							}
						}


					}
				}

				mm->ReleaseInterface(balancer);
			}
		}

		UNLOCK();
	}

	return can_change;
}

local int get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
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

local int find_freq(Arena *arena, Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	int max_freq;
	int desired_teams;
	int include_spec;
	int i;
	Freq *best_freq = NULL;
	ConfigHandle ch;

	ch = arena->cfg;

	/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
	 * Whether to include spectators when enforcing maximum freq sizes. */
	include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

	/* cfghelp: Team:MaxFrequency, arena, int, range: 1-10000, def: 10000
	 * One more than the highest frequency allowed. Set this below
	 * PrivFreqStart to disallow private freqs. */
	max_freq = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);

	desired_teams = cfg->GetInt(arena->cfg, "Team", "DesiredTeams", 2);

	/* search only within the minimum of desired_teams and max_freq */
	max_freq = desired_teams < max_freq ? desired_teams : max_freq;

	for (i = 0; i < max_freq; i++)
	{
		if (!is_freq_full(arena, ch, i, include_spec))
		{
			Freq *freq = get_freq(arena, i);
			if (!freq)
			{
				/* found an empty freq */
				return i;
			}
			else if (freq->metric_sum <= get_max_metric(arena, i) - data->metric)
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
	lm->LogP(L_DRIVEL, "freqman", p, "Entering Initial");
	Arena *arena = p->arena;
	int f = *freq;
	int s = *ship;
	int max_playing, initial_spec, include_spec;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	/* cfghelp: General:MaxPlaying, arena, int, def: 100
	 * This is the most players that will be allowed to play in the arena
	 * at once. Zero means no limit. */
	max_playing = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/* cfghelp: Team:InitialSpec, arena, bool, def: 0
	 * If players entering the arena are always assigned to spectator mode. */
	initial_spec = cfg->GetInt(ch, "Team", "InitialSpec", 0);

	include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

	if ((max_playing > 0
			&& max_playing <= count_current_playing(arena, include_spec))
			|| p->flags.no_ship || !screen_res_allowed(p, ch, NULL, 0)
			|| initial_spec)
	{
		s = SHIP_SPEC;
	}

	update_metric(p);

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
			mask = get_allowable_ships(arena, p, s, f, NULL, 0);
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
	lm->LogP(L_DRIVEL, "freqman", p, "Entering ShipChange");
	Arena *arena = p->arena;
	int max_playing, include_spec;
	ConfigHandle ch;
	int freq = p->p_freq;

	if (!arena) return;

	ch = arena->cfg;

	max_playing = cfg->GetInt(ch, "General", "MaxPlaying", 100);
	include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

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
	else if (!screen_res_allowed(p, ch, err_buf, buf_len))
	{
		/* their resolution exceeds the arena limits */
		/* don't print to err_buf, screen_res_allowed takes care of it */
		return;
	}
	else if (p->p_ship == SHIP_SPEC && max_playing > 0
			&& count_current_playing(arena, include_spec) >= max_playing)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people playing.");
		return;
	}

	/* make sure their ship is legal */
	if (requested_ship != SHIP_SPEC)
	{
		int mask = get_allowable_ships(arena, p, requested_ship, freq, err_buf, buf_len);
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
			snprintf(err_buf, buf_len, "All public frequencies are full!");
		}
	}

	game->SetShipAndFreq(p, requested_ship, freq);
}

local void FreqChange(Player *p, int requested_freq, char *err_buf, int buf_len)
{
	lm->LogP(L_DRIVEL, "freqman", p, "Entering FreqChange");
	Arena *arena = p->arena;
	int ship = p->p_ship;
	int include_spec, max_freq, max_playing;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);
	max_freq = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);
	max_playing = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/* special case: speccer re-entering spec freq */
	if (ship == SHIP_SPEC && requested_freq == arena->specfreq)
	{
		game->SetFreq(p, arena->specfreq);
		return;
	}

	/* check if this change was from the specfreq, and if there are too
	 * many people playing. */
	if (include_spec && ship == SHIP_SPEC && max_playing > 0
			&& p->p_freq == arena->specfreq
			&& count_current_playing(arena, include_spec) >= max_playing)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people playing in this arena.");
		return;
	}

	if (requested_freq < 0 || requested_freq >= max_freq)
	{
		/* he requested a bad freq. */
		if (ship == SHIP_SPEC && p->p_freq != arena->specfreq)
		{
			/* send him back to spec freq */
			game->SetFreq(p, arena->specfreq);
			return;
		}
		else
		{
			snprintf(err_buf, buf_len, "Bad frequency.");
			return;
		}
	}
	else
	{
		if (!can_change_freq(arena, p, requested_freq, err_buf, buf_len))
		{
			/* enforcers won't let them change */
			return;
		}
	}

	/* check if there are too many on the freq */
	if ((ship != SHIP_SPEC || include_spec) && is_freq_full(arena, ch, requested_freq, include_spec))
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "There are too many people on that frequency.");
		return;
	}

	/* make sure their ship is legal */
	if (ship != SHIP_SPEC)
	{
		int mask = get_allowable_ships(arena, p, ship, requested_freq, err_buf, buf_len);
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

			game->SetShipAndFreq(p, new_ship, requested_freq);
		}
	}
	else
	{
		game->SetFreq(p, requested_freq);
	}

	// update_freq is called by the ship or freq change callback
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
	}
	else if (action == AA_DESTROY)
	{
		LOCK();
		LLEnumNC(&ad->freqs, freq_free_enum);
		LLEmpty(&ad->freqs);
		UNLOCK();
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

