
/* dist: public */

#include <string.h>
#include <limits.h>
#include <stdio.h>

#include "asss.h"

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *lm;
local Igame *game;

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

	return max >= count_freq(arena, freq, NULL, include_spec);
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

local int can_change_freq(Arena *arena, Player *p, int new_freq, char *err_buf, int buf_len)
{
	LinkedList list;
	Link *link;
	int can_change = 1;

	LLInit(&list);

	mm->GetAllInterfaces(I_ENFORCER, arena, &list);

	for (link = LLGetHead(&list); link; link = link->next)
	{
		Ienforcer *enforcer = link->data;
		if (!enforcer->CanChangeFreq(p, new_freq, err_buf, buf_len))
		{
			can_change = 0;
			break;
		}
	}

	mm->FreeInterfacesResult(&list);

	/* TODO: check balancer */

	return can_change;
}

local int get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	LinkedList list;
	Link *link;
	int mask = 255;

	LLInit(&list);
	
	mm->GetAllInterfaces(I_ENFORCER, arena, &list);

	for (link = LLGetHead(&list); link; link = link->next)
	{
		Ienforcer *enforcer = link->data;
		
		mask &= enforcer->GetAllowableShips(p, ship, freq, err_buf, buf_len);

		if (mask == 0)
		{
			/* the player can't use any ships, might as well stop looping */
			break;
		}
	}

	mm->FreeInterfacesResult(&list);

	return mask;
}

local int find_freq(Arena *arena, Player *p)
{
	int max_freq;
	ConfigHandle ch;

	ch = arena->cfg;

	/* cfghelp: Team:MaxFrequency, arena, int, range: 1-10000, def: 10000
	 * One more than the highest frequency allowed. Set this below
	 * PrivFreqStart to disallow private freqs. */
	max_freq = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);

	/* TODO */
	return 0;
}

local void Initial(Player *p, int *ship, int *freq)
{
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

	/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
	 * Whether to include spectators when enforcing maximum freq sizes. */
	include_spec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

	if ((max_playing > 0 
			&& max_playing <= count_current_playing(arena, include_spec))
			|| p->flags.no_ship || !screen_res_allowed(p, ch, NULL, 0)
			|| initial_spec)
	{
		s = SHIP_SPEC;
	}

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
			f = arena->specfreq;
	}

	*ship = s; *freq = f;
}

local void ShipChange(Player *p, int requested_ship, char *err_buf, int buf_len)
{
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
		game->SetFreqAndShip(p, SHIP_SPEC, arena->specfreq);
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
		/* don't print a message, screen_res_allowed takes care of it */
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
	}

	game->SetFreqAndShip(p, requested_ship, freq);
}

local void FreqChange(Player *p, int requested_freq, char *err_buf, int buf_len)
{
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

	if (requested_freq < 0 || requested_freq >= max_freq)
	{
		/* he requested a bad freq. drop him elsewhere. */
		requested_freq = find_freq(arena, p);

		/* no freqs available, spec */
		if (requested_freq < 0 || requested_freq >= max_freq)
		{
			game->SetFreqAndShip(p, SHIP_SPEC, arena->specfreq);
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

			game->SetFreqAndShip(p, new_ship, requested_freq);
		}
	}
	else
	{
		game->SetFreq(p, requested_freq);
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
		return MM_OK;

	}
	else if (action == MM_UNLOAD)
	{
		if (fm_int.head.refcount)
			return MM_FAIL;
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&fm_int, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&fm_int, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

