
/* dist: public */

#include <stdlib.h>

#include "asss.h"


typedef struct
{
	struct
	{
		/* if the avg ping is over this, spec */
		int tospec;
		/* if avg ping is over this, start ignoring weapons */
		int wpnstart;
		/* if it's this high or more, ignore all weapons */
		int wpnall;
		/* if it's this high, drop/can't pick up flags or balls */
		int noflags;
	} ping;
	struct
	{
		/* spec if ploss (s2c) gets over this */
		double tospec;
		/* start and finish ignoring weapons */
		double wpnstart, wpnall;
		/* can't pick up flags/balls */
		double noflags;
	} s2closs, wpnloss;
	struct
	{
		/* spec if c2s/wpn ploss gets over this */
		double tospec;
		double noflags;
	} c2sloss;
	int spiketospec;
	int specfreq;
} laglimits_t;

local int cfg_checkinterval;
local laglimits_t *limits[MAXARENA];
local unsigned lastchecked[MAXPLAYERS];

local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilagquery *lag;
local Igame *game;
local Ichat *chat;
local Inet *net;
local Ilogman *lm;



local int Spec(int pid, laglimits_t *ll, const char *why)
{
	if (pd->players[pid].shiptype != SPEC)
	{
		game->SetFreqAndShip(pid, SPEC, ll->specfreq);
		if (lm)
			lm->LogP(L_INFO, "lagaction", pid, "specced for: %s", why);
		return 1;
	}
	else
		return 0;
}


local void check_lag(int pid, laglimits_t *ll)
{
	struct PingSummary pping, cping, rping;
	struct PLossSummary ploss;
	int avg;
	double ign1, ign2, ign3;

	/* gather all data */
	lag->QueryPPing(pid, &pping);
	lag->QueryCPing(pid, &cping);
	lag->QueryRPing(pid, &rping);
	lag->QueryPLoss(pid, &ploss);

	/* weight reliable ping twice the s2c and c2s */
	avg = (pping.avg + cping.avg + 2*rping.avg) / 4;

	/* try to spec people */
	if (avg > ll->ping.tospec)
	{
		if (Spec(pid, ll, "ping") && chat)
			chat->SendMessage(pid,
					"You have been specced for excessive ping (%d > %d)",
					avg, ll->ping.tospec);
	}
	if (ploss.s2c > ll->s2closs.tospec)
	{
		if (Spec(pid, ll, "s2c ploss") && chat)
			chat->SendMessage(pid,
					"You have been specced for excessive S2C packetloss (%.2f > %.2f)",
					100.0 * ploss.s2c, 100.0 * ll->s2closs.tospec);
	}
	if (ploss.s2cwpn > ll->wpnloss.tospec)
	{
		if (Spec(pid, ll, "s2cwpn ploss") && chat)
			chat->SendMessage(pid,
					"You have been specced for excessive S2C weapon packetloss (%.2f > %.2f)",
					100.0 * ploss.s2cwpn, 100.0 * ll->wpnloss.tospec);
	}
	if (ploss.c2s > ll->c2sloss.tospec)
	{
		if (Spec(pid, ll, "c2s ploss") && chat)
			chat->SendMessage(pid,
					"You have been specced for excessive C2S packetloss (%.2f > %.2f)",
					100.0 * ploss.c2s, 100.0 * ll->c2sloss.tospec);
	}

	/* handle ignoring flags/balls */
	if (avg > ll->ping.noflags ||
	    ploss.s2c > ll->s2closs.noflags ||
	    ploss.s2cwpn > ll->wpnloss.noflags ||
	    ploss.c2s > ll->c2sloss.noflags)
		SET_NO_FLAGS_BALLS(pid);
	else
		UNSET_NO_FLAGS_BALLS(pid);

	/* calculate weapon ignore percent */
	ign1 =
		(double)(avg - ll->ping.wpnstart) /
		(double)(ll->ping.wpnall - ll->ping.wpnstart);
	ign2 =
		(double)(ploss.s2c - ll->s2closs.wpnstart) /
		(double)(ll->s2closs.wpnall - ll->s2closs.wpnstart);
	ign3 =
		(double)(ploss.s2cwpn - ll->wpnloss.wpnstart) /
		(double)(ll->wpnloss.wpnall - ll->wpnloss.wpnstart);

	/* we want to ignore the max of these three */
	if (ign2 > ign1) ign1 = ign2;
	if (ign3 > ign1) ign1 = ign3;
	if (ign1 < 0.0) ign1 = 0.0;
	if (ign1 > 1.0) ign1 = 1.0;

	pd->players[pid].ignoreweapons = (unsigned)((double)RAND_MAX * ign1);
}


local void check_spike(int pid, laglimits_t *ll)
{
	int spike = net->GetLastPacketTime(pid) * 10;
	if (spike > ll->spiketospec)
		if (Spec(pid, ll, "spike") && chat)
			chat->SendMessage(pid,
					"You have been specced for a %dms spike",
					spike);
}


local void mainloop()
{
	int pid, arena;
	laglimits_t *ll;
	unsigned now = GTC();

	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (pd->players[pid].status == S_PLAYING &&
		    (now - lastchecked[pid]) > cfg_checkinterval)
		{
			lastchecked[pid] = now;

			arena = pd->players[pid].arena;
			if (ARENA_BAD(arena) || (ll = limits[arena]) == NULL)
				continue;

			check_spike(pid, ll);
			check_lag(pid, ll);
		}
}


local void arenaaction(int arena, int action)
{
	laglimits_t *oldlimits = limits[arena];

	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		ConfigHandle ch = aman->arenas[arena].cfg;
		laglimits_t *ll = amalloc(sizeof(*ll));

#define DOINT(field, key, def) \
		ll->field = cfg->GetInt(ch, "Lag", key, def)
#define DODBL(field, key, def) \
		ll->field = (double)cfg->GetInt(ch, "Lag", key, def) / 1000.0

		/* cfghelp: Lag:PingToSpec, arena, int, def: 600
		 * The average ping at which to force a player to spec. */
		DOINT(ping.tospec, "PingToSpec", 600);
		/* cfghelp: Lag:PingToStartIgnoringWeapons, arena, int, def: 300
		 * The average ping to start ignoring weapons at. */
		DOINT(ping.wpnstart, "PingToStartIgnoringWeapons", 300);
		/* cfghelp: Lag:PingToIgnoreAllWeapons, arena, int, def: 1000
		 * The average ping when all weapons should be ignored. */
		DOINT(ping.wpnall, "PingToIgnoreAllWeapons", 1000);
		/* cfghelp: Lag:PingToDisallowFlags, arena, int, def: 500
		 * The average ping when a player isn't allowed to pick up flags
		 * or balls. */
		DOINT(ping.noflags, "PingToDisallowFlags", 500);

		/* cfghelp: Lag:S2CLossToSpec, arena, int, def: 150
		 * The S2C packetloss at which to force a player to spec. Units
		 * 0.1%. */
		DODBL(s2closs.tospec, "S2CLossToSpec", 150); /* 15% */
		/* cfghelp: Lag:S2CLossToStartIgnoringWeapons, arena, int, def: 40
		 * The S2C packetloss to start ignoring weapons at. Units 0.1%. */
		DODBL(s2closs.wpnstart, "S2CLossToStartIgnoringWeapons", 40);
		/* cfghelp: Lag:S2CLossToIgnoreAllWeapons, arena, int, def: 500
		 * The S2C packetloss when all weapons should be ignored. Units
		 * 0.1%. */
		DODBL(s2closs.wpnall, "S2CLossToIgnoreAllWeapons", 500);
		/* cfghelp: Lag:S2CLossToDisallowFlags, arena, int, def: 50
		 * The S2C packetloss when a player isn't allowed to pick up
		 * flags or balls. Units 0.1%. */
		DODBL(s2closs.noflags, "S2CLossToDisallowFlags", 50);

		/* cfghelp: Lag:WeaponLossToSpec, arena, int, def: 150
		 * The weapon packetloss at which to force a player to spec.
		 * Units 0.1%. */
		DODBL(wpnloss.tospec, "WeaponLossToSpec", 150);
		/* cfghelp: Lag:WeaponLossToStartIgnoringWeapons, arena, int, \
		 * def: 40
		 * The weapon packetloss to start ignoring weapons at. Units
		 * 0.1%. */
		DODBL(wpnloss.wpnstart, "WeaponLossToStartIgnoringWeapons", 40);
		/* cfghelp: Lag:WeaponLossToIgnoreAllWeapons, arena, int, def: 500
		 * The weapon packetloss when all weapons should be ignored.
		 * Units 0.1%. */
		DODBL(wpnloss.wpnall, "WeaponLossToIgnoreAllWeapons", 500);
		/* cfghelp: Lag:WeaponLossToDisallowFlags, arena, int, def: 50
		 * The weapon packetloss when a player isn't allowed to pick up
		 * flags or balls. Units 0.1%. */
		DODBL(wpnloss.noflags, "WeaponLossToDisallowFlags", 50);

		/* cfghelp: Lag:C2SLossToSpec, arena, int, def: 150
		 * The C2S packetloss at which to force a player to spec.
		 * Units 0.1%. */
		DODBL(c2sloss.tospec, "C2SLossToSpec", 150);
		/* cfghelp: Lag:C2SLossToDisallowFlags, arena, int, def: 50
		 * The C2S packetloss when a player isn't allowed to pick up
		 * flags or balls. Units 0.1%. */
		DODBL(c2sloss.noflags, "C2SLossToDisallowFlags", 50);
		/* cfghelp: Lag:SpikeToSpec, arena, int, def: 3000
		 * The amount of time the server can get no data from a player
		 * before forcing him to spectator mode (in ms). */
		DOINT(spiketospec, "SpikeToSpec", 3000);

		/* cache this for later */
		ll->specfreq = cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025);

		limits[arena] = ll;
	}
	else if (action == AA_DESTROY)
		limits[arena] = NULL;

	if (action == AA_CREATE || action == AA_DESTROY || action == AA_CONFCHANGED)
		afree(oldlimits);
}


EXPORT int MM_lagaction(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lag = mm->GetInterface(I_LAGQUERY, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!pd || !aman || !cfg || !lag || !game || !net) return MM_FAIL;

		/* cfghelp: Lag:CheckInterval, global, int, def: 300
		 * How often to check each player for out-of-bounds lag values
		 * (in ticks). */
		cfg_checkinterval = cfg->GetInt(GLOBAL, "Lag", "CheckInterval", 300);

		mm->RegCallback(CB_MAINLOOP, mainloop, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, arenaaction, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		int i;

		/* don't leave stuff like this lying around */
		for (i = 0; i < MAXPLAYERS; i++)
		{
			UNSET_NO_FLAGS_BALLS(i);
			pd->players[i].ignoreweapons = 0;
		}

		mm->UnregCallback(CB_MAINLOOP, mainloop, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, arenaaction, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lag);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

