
#include <string.h>

#include "asss.h"


#define CAP_SEESYSOPLOGALL "seesysoplogall"
#define CAP_SEESYSOPLOGARENA "seesysoplogarena"

enum
{
	SEE_NONE,
	SEE_ARENA,
	SEE_ALL
};


local void LogSysop(char, char *);
local void PA(int pid, int action, int arena);

local byte seewhat[MAXPLAYERS];

local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *log;
local Ichat *chat;
local Icapman *capman;


EXPORT int MM_log_sysop(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CHAT, &chat);
		mm->RegInterest(I_CAPMAN, &capman);

		if (!cfg || !log) return MM_FAIL;

		mm->RegCallback(CB_LOGFUNC, LogSysop, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, PA, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, PA, ALLARENAS);
		mm->UnregCallback(CB_LOGFUNC, LogSysop, ALLARENAS);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &cfg);
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_CAPMAN, &capman);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void LogSysop(char lev, char *s)
{
	if (log->FilterLog(lev, s, "log_sysop"))
	{
		int set[MAXPLAYERS], setc = 0, pid;

		for (pid = 0; pid < MAXPLAYERS; pid++)
		{
			if (seewhat[pid] == SEE_ALL)
				set[setc++] = pid;
			else if (seewhat[pid] == SEE_ARENA)
			{
				/* now we have to check if the arena matches. it kinda
				 * sucks that it has to be done this way, but to do it
				 * the "right" way would require undesirable changes. */
				int arena = pd->players[pid].arena;
				if (ARENA_OK(arena))
				{
					char *carena = aman->arenas[arena].name;
					char *t = strchr(s, '{');
					if (t)
					{
						while (*carena && *t && *t == *carena)
							t++, carena++;
						if (*carena == '\0' && *t == '}')
							set[setc++] = pid;
					}
				}
			}
		}
		set[setc] = -1;
		if (setc)
			chat->SendAnyMessage(set, MSG_SYSOPWARNING, 0, "%s", s);
	}
}


void PA(int pid, int action, int arena)
{
	if (action == PA_CONNECT || action == PA_ENTERARENA)
	{
		if (capman->HasCapability(pid, CAP_SEESYSOPLOGALL))
			seewhat[pid] = SEE_ALL;
		else if (capman->HasCapability(pid, CAP_SEESYSOPLOGARENA))
			seewhat[pid] = SEE_ARENA;
		else
			seewhat[pid] = SEE_NONE;
	}
	else
		seewhat[pid] = SEE_NONE;
}


