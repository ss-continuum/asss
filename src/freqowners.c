
#include "asss.h"


/* commands */
local void Cgiveowner(const char *, int, int);
local void Cfreqkick(const char *, int, int);

/* callbacks */
local void MyPA(int pid, int action, int arena);
local void MyFreqCh(int pid, int newfreq);
local void MyShipCh(int pid, int newship, int newfreq);

/* data */
local char ownsfreq[MAXPLAYERS];

local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmd;
local Iconfig *cfg;
local Ichat *chat;
local Imodman *mm;


EXPORT int MM_freqowners(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_GAME, &game);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_CHAT, &chat);

		mm->RegCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->RegCallback(CB_FREQCHANGE, MyFreqCh, ALLARENAS);
		mm->RegCallback(CB_SHIPCHANGE, MyShipCh, ALLARENAS);

		cmd->AddCommand("giveowner", Cgiveowner);
		cmd->AddCommand("freqkick", Cfreqkick);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("giveowner", Cgiveowner);
		cmd->RemoveCommand("freqkick", Cfreqkick);
		mm->UnregCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->UnregCallback(CB_FREQCHANGE, MyFreqCh, ALLARENAS);
		mm->UnregCallback(CB_SHIPCHANGE, MyShipCh, ALLARENAS);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_GAME, &game);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_CHAT, &chat);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


local int CountFreq(int arena, int freq, int excl)
{
	int t = 0, i;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].arena == arena &&
		    pd->players[i].freq == freq &&
		    i != excl)
			t++;
	pd->UnlockStatus();
	return t;
}


void Cgiveowner(const char *params, int pid, int target)
{
	if (PID_BAD(pid) || PID_BAD(target))
		return;

	if (ownsfreq[pid] &&
	    pd->players[pid].freq == pd->players[target].freq)
		ownsfreq[target] = 1;
}


void Cfreqkick(const char *params, int pid, int target)
{
	if (PID_BAD(pid) || PID_BAD(target))
		return;

	if (ownsfreq[pid] &&
	    pd->players[pid].freq == pd->players[target].freq)
	{
		game->SetShip(target, SPEC);
		chat->SendMessage(target, "You have been kicked off the freq by %s",
				pd->players[pid].name);
	}
}


void MyPA(int pid, int action, int arena)
{
	ownsfreq[pid] = 0;
}


void MyFreqCh(int pid, int newfreq)
{
	int arena = pd->players[pid].arena;
	ConfigHandle ch;

	ch = aman->arenas[arena].cfg;

	if (CountFreq(arena, newfreq, pid) == 0 &&
	    cfg->GetInt(ch, "Team", "AllowFreqOwners", 1) &&
	    newfreq >= cfg->GetInt(ch, "Team", "PrivFreqStart", 100) &&
	    newfreq != cfg->GetInt(ch, "Team", "SpectatorFrequency", 8025))
	{
		ownsfreq[pid] = 1;
		chat->SendMessage(pid, "You are the now the owner of freq %d. "
				"You can kick people off your freq by sending them "
				"the private message \"?freqkick\", and you can share "
				"your ownership by sending people \"?giveowner\".", newfreq);
	}
	else
		ownsfreq[pid] = 0;
}


void MyShipCh(int pid, int newship, int newfreq)
{
	MyFreqCh(pid, newfreq);
}


