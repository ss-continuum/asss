
#include "asss.h"


local void MyFreqManager(int pid, int request, int *ship, int *freq);

local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Imodman *mm;


int MM_fm_normal(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_FREQMANAGER, MyFreqManager, arena);
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_FREQMANAGER, MyFreqManager, arena);
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}



local int CountFreq(int arena, int freq, int excl, int inclspec)
{
	int t = 0, i;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].arena == arena &&
		    pd->players[i].freq == freq &&
		    i != excl &&
		    ( pd->players[i].shiptype < SPEC || inclspec ) )
			t++;
	pd->UnlockStatus();
	return t;
}


local int FindLegalShip(int arena, int freq, int ship)
{
	int clockwork = cfg->GetInt(aman->arenas[arena].cfg,
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


#define MAXDES 10

local int BalanceFreqs(int arena, int excl, int inclspec)
{
	int counts[MAXDES] = { 0 }, i, desired, min = MAXPLAYERS, best = -1, max;

	max = cfg->GetInt(aman->arenas[arena].cfg, "Team", "MaxPerTeam", 0);
	desired = cfg->GetInt(aman->arenas[arena].cfg,
			"Team", "DesiredTeams", 1);

	if (desired < 1) desired = 1;
	if (desired > MAXDES) desired = MAXDES;

	/* get counts */
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].arena == arena &&
		    pd->players[i].freq < desired &&
		    i != excl &&
		    ( pd->players[i].shiptype < SPEC || inclspec ) )
			counts[pd->players[i].freq]++;
	pd->UnlockStatus();

	for (i = 0; i < desired; i++)
		if (counts[i] < min)
		{
			min = counts[i];
			best = i;
		}

	if (best == -1) /* shouldn't happen */
		return 0;
	else if (max == 0 || best < max) /* we found a spot */
		return best;
	else /* no spots within desired freqs */
	{
		/* try incrementing freqs until we find one with < max players */
		i = desired;
		while (CountFreq(arena, i, excl, inclspec) >= max)
			i++;
		return i;
	}
}


void MyFreqManager(int pid, int request, int *ship, int *freq)
{
	int arena, specfreq, f = *freq, s = *ship;
	ConfigHandle ch;

	arena = pd->players[pid].arena;

	if (arena < 0 || arena >= MAXARENA) return;

	ch = aman->arenas[arena].cfg;
	specfreq = cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025);

	if (request == REQUEST_INITIAL)
	{
		if (s < SPEC)
		{
			f = cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025);
		}
		else
		{
			/* we have to assign him to a freq */
			int inclspec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);
			f = BalanceFreqs(arena, pid, inclspec);
			/* and make sure the ship is still legal */
			s = FindLegalShip(arena, f, s);
		}
	}
	else if (request == REQUEST_SHIP)
	{
		if (s >= SPEC)
		{
			/* if he's switching to spec, it's easy */
			f = specfreq;
		}
		else
		{
			/* he's changing to a ship. check if he's changing from spec */
			int oldfreq = pd->players[pid].freq;
			if (oldfreq == specfreq)
			{
				/* we have to assign him to a freq */
				int inclspec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);
				f = BalanceFreqs(arena, pid, inclspec);
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
	else /* REQUEST_FREQ */
	{
		int count, max;
		int inclspec = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);
		int maxfreq = cfg->GetInt(ch, "Team", "MaxFrequency", 9999);
		int privlimit = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);

		if (f >= privlimit)
			max = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 0);
		else
			max = cfg->GetInt(ch, "Team", "MaxPerTeam", 0);

		/* special case: speccer re-entering spec freq */
		if (s == SPEC && f == specfreq)
			return;

		if (f < 0 || f > maxfreq)
			/* he requested a bad freq. drop him elsewhere. */
			f = BalanceFreqs(arena, pid, inclspec);
		else
		{
			/* check to make sure the new freq is ok */
			count = CountFreq(arena, f, pid, inclspec);
			if (max > 0 && count >= max)
				/* the freq has too many people, assign him to another */
				f = BalanceFreqs(arena, pid, inclspec);
		}
		/* make sure he has an appropriate ship for this freq */
		s = FindLegalShip(arena, f, s);
	}

	*ship = s; *freq = f;
}



