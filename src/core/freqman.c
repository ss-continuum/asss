
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

local int count_current_playing(Arena *arena, int inclspec)
{
	Player *p;
	Link *link;
	int playing = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && p->arena == arena 
				&& (p->p_ship != SHIP_SPEC || inclspec)
				&& IS_HUMAN(p))
			playing++;
	pd->Unlock();
	return playing;
}

local int count_freq(Arena *arena, int freq, Player *excl, int inclspec)
{
	Player *p;
	Link *link;
	int t = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == arena && p->p_freq == freq && p != excl && 
				(p->p_ship != SHIP_SPEC || inclspec))
			t++;
	pd->Unlock();
	return t;
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

	snprintf(err_buf, buf_len, "Maximum allowed screen resolution is %dx%d in this arena", max_x, max_y);
	return 0;
}

local int get_enforcer_masks(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	LinkedList list;
	Link *link;
	int mask = 255;

	/*LLInit(&list);
	mm->GetInterfaces(I_ENFORCER, arena, list);

	for (link = LLGetHead(&list); link; link = link->next)
	{
		Ienforer *enforcer = link->data;
		if (!enforcer)
		{
			lm->LogA(L_WARN, "freqman", arena, "Got a null module from modman!");
			continue;
		}

		/ * only check the enforcer if we didn't already get a -1 * /
		if (mask != -1)
		{
			int result = enforcer->GetShipMask(p, ship, freq, err_buf, buf_len);

			if (result == -1)
			{
				mask = -1;
			}
			else
			{
				mask = mask & result;
			}
		}

		mm->ReleaseInterface(enforcer);
	}*/

	return mask;
}

local void InitialFreq(Player *p, int *ship, int *freq, char *err_buf, int buf_len)
{
	/*Arena *arena = p->arena;
	int f, s = *ship;
	int initial_spec;
	int maxplaying;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	/ * cfghelp: General:MaxPlaying, arena, int, def: 100
	 * This is the most players that will be allowed to play in the arena
	 * at once. Zero means no limit. * /
	maxplaying = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/ * cfghelp: Team:InitialSpec, arena, bool, def: 0
	 * If players entering the arena are always assigned to spectator mode. * /
	initial_spec = cfg->GetInt(ch, "Team", "InitialSpec", 0);

	if ((maxplaying > 0 && maxplaying <= count_current_playing(arena, / *TODO* / 0)) ||
		p->flags.no_ship || 
		!screen_res_allowed(p, ch, err_buf, buf_len) ||	
		initial_spec)
	{
		s = SHIP_SPEC;
	}

	if (s == SHIP_SPEC)
	{
		/ * don't check the enforcers since the player is going to the 
		 * spec freq * /
		f = arena->specfreq;
	}
	else
	{
		/ * TODO: find an inital public frequency using the balancer * /
		/ * TODO: run it through the enforcer, specfreq if freq fails or no avaliable ships * /
	}

	*ship = s; *freq = f;*/
}

local void ShipChange(Player *p, int *ship, int *freq, char *err_buf, int buf_len)
{
	/*Arena *arena = p->arena;
	int f = *freq, s = *ship;
	int maxplaying;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;
	maxplaying = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	if (s >= SHIP_SPEC)
	{
		/ * always allow switching to spec * /
		f = arena->specfreq;
	}
	else if (p->flags.no_ship)
	{
		/ * too much lag to get in a ship * /
		snprintf(err_buf, buf_len, "Too much lag to play in this arena.");
		goto deny;
	}
	else if (!screen_res_allowed(p, ch, err_buf, buf_len))
	{
		/ * their resolution exceeds the arena limits * /
		goto deny;
	}
	else if (p->p_ship == SHIP_SPEC && maxplaying > 0 &&
			count_current_playing(arena) >= maxplaying)
	{
		snprintf(err_buf, buf_len, "There are too many people playing.");
		goto deny;
	}
	else
	{
		/ * allowed to change * /
		/ * TODO * /
	}

	*ship = s; *freq = f;
	return;

deny:
	*ship = p->p_ship;
	*freq = p->p_freq;*/
}

local void FreqChange(Player *p, int *ship, int *freq, char *err_buf, int buf_len)
{
	/*Arena *arena = p->arena;
	int f = *freq, s = *ship;
	int inclspec, maxfreq, maxplaying;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	/ * cfghelp: Team:IncludeSpectators, arena, bool, def: 0
	 * Whether to include spectators when enforcing maximum freq sizes. * /
	inclspec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);
	/ * cfghelp: Team:MaxFrequency, arena, int, range: 1-10000, def: 10000
	 * One more than the highest frequency allowed. Set this below
	 * PrivFreqStart to disallow private freqs. * /
	maxfreq = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);
	maxplaying = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/ * special case: speccer re-entering spec freq * /
	if (s == SHIP_SPEC && f == arena->specfreq)
		return;
	
	if (f < 0 || f >= maxfreq)
	{
		/ * he requested a bad freq. drop him elsewhere. * /
		f = 0; / * TODO find a freq to balance * /
		if (f < 0 || f >= maxfreq)
		{
			f = arena->specfreq;
			s = SHIP_SPEC;
		}
	}
	else
	{
		/ * TODO * /
	}

	/ * TODO: find legal ship * /

        / * check if this change brought him out of spec and there are too
	 * many people playing. * /
	if (s != SHIP_SPEC && p->p_ship == SHIP_SPEC && maxplaying > 0 &&
		count_current_playing(arena) >= maxplaying)
	{
		s = p->p_ship;
		f = p->p_freq;
		snprintf(err_buf, buf_len, "There are too many people playing in this arena.");
	}

	*ship = s; *freq = f; */
}

local Ifreqman fm_int =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "freqman")
	InitialFreq, ShipChange, FreqChange
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
		if (!lm || !pd || !aman || !cfg)
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

