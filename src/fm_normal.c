
/* dist: public */

#include <string.h>
#include <limits.h>

#include "asss.h"


/* cfghelp: Team:SpectatorFrequency, arena, int, range: 0-9999, def: 8025
 * The frequency that spectators are assigned to, by default. */
#define SPECFREQ(ch) cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025)

/* cfghelp: Team:InitalSpec, arena, bool, def: 0
 * If players entering the arena are always assigned to spectator mode. */
#define INITIALSPEC(ch) cfg->GetInt(ch, "Team", "InitalSpec", 0)

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


typedef struct pdata
{
	byte lockship;
} pdata;

typedef struct adata
{
	byte initlockship;
	byte initspec;
} adata;


local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Icapman *capman;
local Imodman *mm;

local int adkey, pdkey;


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
	int max_x = MAXXRES(ch);
	int max_y = MAXYRES(ch);
	if((max_x == 0 || p->xres <= max_x) && (max_y == 0 || p->yres <= max_y))
		return 1;

	if (chat)
		chat->SendMessage(p,
			"Maximum allowed screen resolution is %dx%d in this arena",
			max_x, max_y);

	return 0;
}

local void Initial(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *pdata = PPDATA(p, pdkey);
	int f, s = *ship;
	ConfigHandle ch;

	if (!arena) return;

	pdata->lockship = ad->initlockship;

	ch = arena->cfg;

	if (count_current_playing(arena) >= MAXPLAYING(ch) ||
	    p->flags.no_ship ||
	    !screen_res_allowed(p, ch) ||
	    INITIALSPEC(ch) ||
	    ad->initspec)
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


local void Ship(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	pdata *pdata = PPDATA(p, pdkey);
	int specfreq, f = *freq, s = *ship;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;
	specfreq = SPECFREQ(ch);

	/* always allow switching to spec */
	if (s >= SPEC)
	{
		f = specfreq;
	}
	/* otherwise, he's changing to a ship */
	/* check lag */
	else if (p->flags.no_ship)
	{
		if (chat)
			chat->SendMessage(p,
					"You have too much lag to play in this arena.");
		goto deny;
	}
	/* allowed res; this prints out its own error message */
	else if (!screen_res_allowed(p, ch))
	{
		goto deny;
	}
	/* check if changing from spec and too many playing */
	else if (p->p_ship == SPEC && count_current_playing(arena) >= MAXPLAYING(ch))
	{
		if (chat)
			chat->SendMessage(p,
					"There are too many people playing in this arena.");
		goto deny;
	}
	/* check locks */
	else if (pdata->lockship && !(capman && capman->HasCapability(p, "bypasslock")))
	{
		if (chat)
			chat->SendMessage(p, "You have been locked in %s.",
					(p->p_ship == SPEC) ? "spectator mode" : "your ship");
		goto deny;
	}
	/* ok, allowed change */
	else
	{
		/* check if he's changing from spec */
		if (p->p_ship == SPEC && p->p_freq == specfreq)
		{
			/* leaving spec, we have to assign him to a freq */
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

	*ship = s; *freq = f;
	return;

deny:
	*ship = p->p_ship;
	*freq = p->p_freq;
}


local void Freq(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	int specfreq, f = *freq, s = *ship;
	int count, max, inclspec, maxfreq, privlimit;
	ConfigHandle ch;

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


/* locking commands */

local void lock_work(const Target *target, int nval, int notify)
{
	LinkedList set = LL_INITIALIZER;
	Link *l;
	pd->TargetToSet(target, &set);
	for (l = LLGetHead(&set); l; l = l->next)
	{
		Player *p = l->data;
		pdata *pdata = PPDATA(p, pdkey);
		if (notify && pdata->lockship != nval && chat)
			chat->SendMessage(p, nval ?
					(p->p_ship == SPEC ?
					 "You have been locked to spectator mode." :
					 "You have been locked to your ship.") :
					"Your ship has been unlocked.");
		pdata->lockship = nval;
	}
	LLEmpty(&set);
}


local helptext_t lock_help =
"Targets: player, freq, or arena\n"
"Args: [-n]\n"
"Locks the specified targets so that they can't change ships. Use ?unlock\n"
"to unlock them. Note that this doesn't change anyone's ship. Use ?setship\n"
"or ?specall for that. If {-n} is present, notifies players of their change\n"
"in status.\n";

local void Clock(const char *params, Player *p, const Target *target)
{
	lock_work(target, TRUE, strstr(params, "-n") != NULL);
}


local helptext_t unlock_help =
"Targets: player, freq, or arena\n"
"Args: [-n]\n"
"Unlocks the specified targets so that they can now change ships. An optional\n"
"{-n} notifies players of their change in status.\n";

local void Cunlock(const char *params, Player *p, const Target *target)
{
	lock_work(target, FALSE, strstr(params, "-n") != NULL);
}


local helptext_t lockarena_help =
"Targets: arena\n"
"Args: [-n] [-a] [-i]\n"
"Changes the default locked state for the arena so entering players will be locked\n"
"to spectator mode. Also locks everyone currently in the arena to their ships. The {-n}\n"
"option means to notify players of their change in status. The {-a} options means to\n"
"only change the arena's state, and not lock current players. The {-i} option means to\n"
"only lock entering players to their initial ships, instead of spectator mode.\n";

local void Clockarena(const char *params, Player *p, const Target *target)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (target->type != T_ARENA) return;
	ad->initlockship = TRUE;
	if (strstr(params, "-i") == NULL)
		ad->initspec = TRUE;
	if (strstr(params, "-a") == NULL)
		lock_work(target, TRUE, strstr(params, "-n") != NULL);
}


local helptext_t unlockarena_help =
"Targets: arena\n"
"Args: [-n] [-a]\n"
"Changes the default locked state for the arena so entering players will not be\n"
"locked to spectator mode. Also unlocks everyone currently in the arena to their ships\n"
"The {-n} options means to notify players of their change in status. The {-a} option\n"
"means to only change the arena's state, and not unlock current players.\n";

local void Cunlockarena(const char *params, Player *p, const Target *target)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (target->type != T_ARENA) return;
	ad->initlockship = FALSE;
	ad->initspec = FALSE;
	if (strstr(params, "-a") == NULL)
		lock_work(target, FALSE, strstr(params, "-n") != NULL);
}


local Ifreqman fm_int =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "fm-normal")
	Initial, Ship, Freq
};

EXPORT int MM_fm_normal(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		if (!pd || !aman || !cfg)
			return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(adata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (adkey == -1 || pdkey == -1)
			return MM_FAIL;
		if (cmd)
		{
			cmd->AddCommand("lock", Clock, lock_help);
			cmd->AddCommand("unlock", Cunlock, unlock_help);
			cmd->AddCommand("lockarena", Clockarena, lockarena_help);
			cmd->AddCommand("unlockarena", Cunlockarena, unlockarena_help);
		}
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (fm_int.head.refcount)
			return MM_FAIL;
		pd->FreePlayerData(pdkey);
		aman->FreeArenaData(adkey);
		if (cmd)
		{
			cmd->RemoveCommand("lock", Clock);
			cmd->RemoveCommand("unlock", Cunlock);
			cmd->RemoveCommand("lockarena", Clockarena);
			cmd->RemoveCommand("unlockarena", Cunlockarena);
		}
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(capman);
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

