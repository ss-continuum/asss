
/* dist: public */

#include <limits.h>

#include "asss.h"


/* cfghelp: Team:SpectatorFrequency, arena, int, range: 0-9999, def: 8025
 * The frequency that spectators are assigned to, by default. */
#define SPECFREQ(ch) cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025)

/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
 * Whether to include spectators when enforcing maximum freq sizes. */
#define INCLSPEC(ch) cfg->GetInt(ch, "Team", "IncludeSpectators", 0)

/* cfghelp: Team:MaxPerTeam, arena, int, def: 0
 * The maximum number of players on a public freq. Zero means no limit. */
#define MAXTEAM(ch) cfg->GetInt(ch, "Team", "MaxPerTeam", 0)

/* cfghelp: General:MaxPlaying, arena, int, def: 100
 * This is the most players that will be allowed to play in the arena at
 * once. Zero means no limit. */
#define MAXPLAYING(ch) cfg->GetInt(ch, "General", "MaxPlaying", 100)

/* cfghelp: Misc:MaxXres, arena, int, def: 0
 * Maximum screen width allowed in the arena. Zero means no limit. */
#define MAXXRES(ch) cfg->GetInt(ch, "Misc", "MaxXres", 0)

/* cfghelp: Misc:MaxYres, arena, int, def: 0
 * Maximum screen height allowed in the arena. Zero means no limit. */
#define MAXYRES(ch) cfg->GetInt(ch, "Misc", "MaxYres", 0)

local void Initial(Player *p, int *ship, int *freq);
local void Ship(Player *p, int *ship, int *freq);
local void Freq(Player *p, int *ship, int *freq);

local Ifreqman _fm =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "fm-normal")
	Initial, Ship, Freq
};

local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Imodman *mm;


EXPORT int MM_fm_normal(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&_fm, arena);
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&_fm, arena);
	}
	return MM_FAIL;
}


local int count_current_playing(Arena *arena)
{
	Player *p;
	Link *link;
	int playing = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->p_ship != SPEC)
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
		if (p->arena == arena &&
		    p->p_freq == freq &&
		    p != excl &&
		    ( p->p_ship < SPEC || inclspec ) )
			t++;
	pd->Unlock();
	return t;
}


local int FindLegalShip(Arena *arena, int freq, int ship)
{
	/* cfghelp: Team:FrequencyShipTypes, arena, bool, def: 0
	 * If this is set, freq 0 will only be allowed to use warbirds, freq
	 * 1 can only use javelins, etc. */
	int clockwork = cfg->GetInt(arena->cfg,
			"Misc", "FrequencyShipTypes", 0);

	if (clockwork)
	{
		/* we don't want to switch the ships of speccers, even in FST */
		if (ship == SPEC || freq < 0 || freq > SHARK)
			return SPEC;
		else
			return freq;
	}
	else
	{
		/* no other options for now */
		return ship;
	}
}


local int BalanceFreqs(Arena *arena, Player *excl, int inclspec)
{
	Player *i;
	Link *link;
	int counts[CFG_MAX_DESIRED] = { 0 }, desired, min = INT_MAX, best = -1, max, j;

	max = MAXTEAM(arena->cfg);
	/* cfghelp: Team:DesiredTeams, arena, int, def: 2
	 * The number of teams that the freq balancer will form as players
	 * enter. */
	desired = cfg->GetInt(arena->cfg,
			"Team", "DesiredTeams", 2);

	if (desired < 1) desired = 1;
	if (desired > CFG_MAX_DESIRED) desired = CFG_MAX_DESIRED;

	/* get counts */
	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->arena == arena &&
		    i->p_freq < desired &&
		    i != excl &&
		    ( i->p_ship < SPEC || inclspec ) )
			counts[i->p_freq]++;
	pd->Unlock();

	for (j = 0; j < desired; j++)
		if (counts[j] < min)
		{
			min = counts[j];
			best = j;
		}

	if (best == -1) /* shouldn't happen */
		return 0;
	else if (max == 0 || best < max) /* we found a spot */
		return best;
	else /* no spots within desired freqs */
	{
		/* try incrementing freqs until we find one with < max players */
		j = desired;
		while (count_freq(arena, j, excl, inclspec) >= max)
			j++;
		return j;
	}
}

local int screen_res_allowed(Player *p, ConfigHandle ch)
{
	int max_x=MAXXRES(ch);
	int max_y=MAXYRES(ch);
	if((!max_x || p->xres <= max_x) && (!max_y || p->yres <= max_y))
		return 1;

	if (chat)
		chat->SendMessage(p,
			"Maximum allowed screen resolution is %dx%d in this arena",
			max_x,max_y);

	return 0;
}

void Initial(Player *p, int *ship, int *freq)
{
	Arena *arena;
	int f, s = *ship;
	ConfigHandle ch;

	arena = p->arena;

	if (!arena) return;

	ch = arena->cfg;

	if (count_current_playing(arena) >= MAXPLAYING(ch) ||
	    p->flags.no_ship ||
	    !screen_res_allowed(p, ch))
		s = SPEC;

	if (s == SPEC)
	{
		f = SPECFREQ(ch);
	}
	else
	{
		/* we have to assign him to a freq */
		int inclspec = INCLSPEC(ch);
		f = BalanceFreqs(arena, p, inclspec);
		/* and make sure the ship is still legal */
		s = FindLegalShip(arena, f, s);
	}

	*ship = s; *freq = f;
}


void Ship(Player *p, int *ship, int *freq)
{
	Arena *arena;
	int specfreq, f = *freq, s = *ship;
	ConfigHandle ch;

	arena = p->arena;

	if (!arena) return;

	ch = arena->cfg;
	specfreq = SPECFREQ(ch);

	if (s >= SPEC)
	{
		/* if he's switching to spec, it's easy */
		f = specfreq;
	}
	else
	{
		/* he's changing to a ship */
		if (count_current_playing(arena) >= MAXPLAYING(ch))
		{
			/* too many playing, cancel request */
			s = p->p_ship;
			f = p->p_freq;
			if (chat)
				chat->SendMessage(p,
						"There are too many people playing in this arena.");
		}
		else if (p->flags.no_ship)
		{
			s = p->p_ship;
			f = p->p_freq;
			if (chat)
				chat->SendMessage(p,
						"You have too much lag to play in this arena.");
		}
		else if (!screen_res_allowed(p, ch))
		{
			s = p->p_ship;
			f = p->p_freq;
		}
		else
		{
			/* check if he's changing from spec */
			int oldfreq = p->p_freq;
			if (oldfreq == specfreq)
			{
				/* we have to assign him to a freq */
				int inclspec = INCLSPEC(ch);
				f = BalanceFreqs(arena, p, inclspec);
				/* and make sure the ship is still legal */
				s = FindLegalShip(arena, f, s);
			}
			else
			{
				/* don't touch freq, but make sure ship is ok */
				s = FindLegalShip(arena, f, s);
			}
		}
	}

	*ship = s; *freq = f;
}


void Freq(Player *p, int *ship, int *freq)
{
	Arena *arena;
	int specfreq, f = *freq, s = *ship;
	int count, max, inclspec, maxfreq, privlimit;
	ConfigHandle ch;

	arena = p->arena;

	if (!arena) return;

	ch = arena->cfg;
	specfreq = SPECFREQ(ch);
	inclspec = INCLSPEC(ch);
	/* cfghelp: Team:MaxFrequency, arena, int, range: 0-9999, def: 9999
	 * The highest frequency allowed. Set this below PrivFreqStart to
	 * disallow private freqs. */
	maxfreq = cfg->GetInt(ch, "Team", "MaxFrequency", 9999);
	/* cfghelp: Team:PrivFreqStart, arena, int, range: 0-9999, def: 100
	 * Freqs above this value are considered private freqs. */
	privlimit = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);

	if (f >= privlimit)
		/* cfghelp: Team:MaxPerPrivateTeam, arena, int, def: 0
		 * The maximum number of players on a private freq. Zero means
		 * no limit. */
		max = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 0);
	else
		max = MAXTEAM(ch);

	/* special case: speccer re-entering spec freq */
	if (s == SPEC && f == specfreq)
		return;

	if (f < 0 || f > maxfreq)
		/* he requested a bad freq. drop him elsewhere. */
		f = BalanceFreqs(arena, p, inclspec);
	else
	{
		/* check to make sure the new freq is ok */
		count = count_freq(arena, f, p, inclspec);
		if (max > 0 && count >= max)
			/* the freq has too many people, assign him to another */
			f = BalanceFreqs(arena, p, inclspec);
	}

	/* make sure he has an appropriate ship for this freq */
	s = FindLegalShip(arena, f, s);

	/* check if this change brought him out of spec and there are too
	 * many people playing. */
	if (s != SPEC &&
	    p->p_ship == SPEC &&
	    count_current_playing(arena) >= MAXPLAYING(ch))
	{
		s = p->p_ship;
		f = p->p_freq;
		if (chat)
			chat->SendMessage(p,
					"There are too many people playing in this arena.");
	}

	*ship = s; *freq = f;
}

