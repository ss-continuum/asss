
#include <string.h>
#include <stdlib.h>

#include "asss.h"


#define MAXWARNMSGS 5


/* interface pointers */
local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *lm;
local Icmdman *cmd;
local Ichat *chat;
local Imainloop *ml;

/* timer data */
local struct
{
	int gamelen;
	int enabled;
	unsigned timeout;
	int warnmsgs[MAXWARNMSGS];
} ar_tmr[MAXARENA];


local int TimerMaster(void *nothing)
{
	unsigned tickcount = GTC();
	int i,j;

	for (i = 0; i < MAXARENA; i++)
		if (ar_tmr[i].enabled && tickcount > ar_tmr[i].timeout)
		{
			lm->LogA(L_DRIVEL, "game_timer", i, "Timer expired");
			DO_CBS(CB_TIMESUP, i, GameTimerFunc, (i));
			chat->SendArenaMessage(i, "Time has expired.");
			if (ar_tmr[i].gamelen)
				ar_tmr[i].timeout = tickcount+ar_tmr[i].gamelen;
			else
			{
				ar_tmr[i].enabled = 0;
				ar_tmr[i].timeout = 0;
			}
		}
		else if (ar_tmr[i].enabled)
		{
			tickcount = (ar_tmr[i].timeout - GTC())/100;
			for (j = 0; j < MAXWARNMSGS; j++)
				if (tickcount && ar_tmr[i].warnmsgs[j] == tickcount)
				{
					if (!(ar_tmr[i].warnmsgs[j]%60))
						chat->SendArenaMessage(i, "NOTICE: %u minute%s remaining.", tickcount/60, tickcount == 60 ? "" : "s");
					else
						chat->SendArenaMessage(i, "NOTICE: %u seconds remaining.", tickcount);
				}
		}
	return TRUE;
}


local void ArenaAction(int arena, int action)
{
	int i;
	const char *warnvals, *tmp = NULL;
	char num[16];

	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		warnvals = cfg->GetStr(aman->arenas[arena].cfg, "Misc", "TimerWarnings");
		for (i = 0; i < MAXWARNMSGS; i++)
			ar_tmr[arena].warnmsgs[i] = 0;
		if (warnvals)
			for (i = 0; i < MAXWARNMSGS && strsplit(warnvals, " ,", num, sizeof(num), &tmp); i++)
				ar_tmr[arena].warnmsgs[i] = strtol(num, NULL, 0);

		ar_tmr[arena].gamelen = cfg->GetInt(aman->arenas[arena].cfg, "Misc", "TimedGame", 0);
		if (action == AA_CREATE && ar_tmr[arena].gamelen)
		{
			ar_tmr[arena].enabled = 1;
			ar_tmr[arena].timeout = GTC()+ar_tmr[arena].gamelen;
		}
	}
	else if (action == AA_DESTROY)
	{
		ar_tmr[arena].enabled = 0;
		ar_tmr[arena].timeout = 0;
	}
}



local helptext_t time_help =
"Targets: none\n"
"Args: none\n"
"Returns amount of time left in current game.\n";

local void Ctime(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena, mins, secs;
	unsigned tout;

	if (ar_tmr[arena].enabled)
	{
		tout = ar_tmr[arena].timeout - GTC();
		mins = tout/60/100;
		secs = (tout/100)%60;
		chat->SendMessage(pid, "Time left: %d minutes %d seconds", mins, secs);
	}
	else if (ar_tmr[arena].timeout)
	{
		 mins = ar_tmr[arena].timeout/60/100;
		 secs = (ar_tmr[arena].timeout/100)%60;
		 chat->SendMessage(pid, "Timer paused at:  %d minutes %d seconds", mins, secs);
	}
	else
		chat->SendMessage(pid, "Time left: 0 minutes 0 seconds");
}


local helptext_t timer_help =
"Targets: none\n"
"Args: <minutes>[:<seconds>]\n"
"Set arena timer to minutes:seconds, only in arenas with TimedGame setting\n"
"off. Note, that the seconds part is optional, but minutes must always\n"
"be defined (even if zero). If successful, server replies with ?time response.\n";

local void Ctimer(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena, mins = 0, secs = 0;

	if (ar_tmr[arena].gamelen == 0)
	{
		char *end;
		mins = strtol(params, &end, 10);
		if (end != params)
		{
			if ((end = strchr(end, ':')))
				secs = strtol(end+1, NULL, 10);
			ar_tmr[arena].enabled = 1;
			ar_tmr[arena].timeout = GTC()+(60*100*mins)+(100*secs);
			Ctime(params, pid, target);
		}
		else chat->SendMessage(pid, "timer format is: '?timer mins[:secs]'");
	}
	else chat->SendMessage(pid, "Timer is fixed to Misc:TimedGame setting.");
}


local helptext_t timereset_help =
"Targets: none\n"
"Args: none\n"
"Reset a timed game, but only in arenas with Misc:TimedGame in use.\n";

local void Ctimereset(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena;
	long gamelen = ar_tmr[arena].gamelen;

	if (gamelen)
	{
		ar_tmr[arena].timeout = GTC() + gamelen;
		Ctime(params, pid, target);
	}
}


local helptext_t pausetimer_help =
"Targets: none\n"
"Args: none\n"
"Pauses the timer. The timer must have been created with ?timer.\n";

local void Cpausetimer(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena;

	if (ar_tmr[arena].gamelen) return;

	if (ar_tmr[arena].enabled)
	{
		ar_tmr[arena].enabled = 0;
		ar_tmr[arena].timeout -= GTC();
		chat->SendMessage(pid,"Timer paused at:  %d minutes %d seconds",
							ar_tmr[arena].timeout/60/100, (ar_tmr[arena].timeout/100)%60);
	}
	else if (ar_tmr[arena].timeout)
	{
		chat->SendMessage(pid,"Timer resumed at: %d minutes %d seconds",
							ar_tmr[arena].timeout/60/100, (ar_tmr[arena].timeout/100)%60);
		ar_tmr[arena].enabled = 1;
		ar_tmr[arena].timeout += GTC();
	}
}



EXPORT int MM_game_timer(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		int i;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);


		for (i = 0; i < MAXARENA; i++)
			ar_tmr[i].enabled = 0;

		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		ml->SetTimer(TimerMaster, 100, 100, NULL);

		cmd->AddCommand("timer", Ctimer, timer_help);
		cmd->AddCommand("time", Ctime, time_help);
		cmd->AddCommand("timereset", Ctimereset, timereset_help);
		cmd->AddCommand("pausetimer", Cpausetimer, pausetimer_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("timer", Ctimer);
		cmd->RemoveCommand("time", Ctime);
		cmd->RemoveCommand("timereset", Ctimereset);
		cmd->RemoveCommand("pausetimer", Cpausetimer);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		ml->ClearTimer(TimerMaster);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
	return MM_FAIL;
}

