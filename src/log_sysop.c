
#include <string.h>
#include <stdlib.h>

#include "asss.h"


#define CAP_SEESYSOPLOGALL "seesysoplogall"
#define CAP_SEESYSOPLOGARENA "seesysoplogarena"


local void LogSysop(char, char *);
local void PA(int pid, int action, int arena);
local void Clastlog(const char *params, int pid, int target);

enum { SEE_NONE, SEE_ARENA, SEE_ALL };
local byte seewhat[MAXPLAYERS];

/* stuff for lastlog */
#define MAXLAST 100
#define MAXLINE 128
/* this is a circular buffer structure */
local int ll_pos;
local char ll_data[MAXLAST][MAXLINE]; /* 12.5k */
local pthread_mutex_t ll_mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_LL() pthread_mutex_lock(&ll_mtx)
#define UNLOCK_LL() pthread_mutex_unlock(&ll_mtx)


local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *log;
local Ichat *chat;
local Icapman *capman;
local Icmdman *cmd;


EXPORT int MM_log_sysop(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface("playerdata", ALLARENAS);
		aman = mm->GetInterface("arenaman", ALLARENAS);
		cfg = mm->GetInterface("config", ALLARENAS);
		log = mm->GetInterface("logman", ALLARENAS);
		chat = mm->GetInterface("chat", ALLARENAS);
		capman = mm->GetInterface("capman", ALLARENAS);
		cmd = mm->GetInterface("cmdman", ALLARENAS);

		if (!cfg || !log || !chat) return MM_FAIL;

		memset(ll_data, 0, sizeof(ll_data));
		ll_pos = 0;

		mm->RegCallback(CB_LOGFUNC, LogSysop, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, PA, ALLARENAS);

		cmd->AddCommand("lastlog", Clastlog);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("lastlog", Clastlog);
		mm->UnregCallback(CB_PLAYERACTION, PA, ALLARENAS);
		mm->UnregCallback(CB_LOGFUNC, LogSysop, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(cmd);
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

	/* always add to lastlog */
	LOCK_LL();
	ll_data[ll_pos][0] = lev;
	ll_data[ll_pos][1] = ':';
	ll_data[ll_pos][2] = ' ';
	astrncpy(ll_data[ll_pos] + 3, s, MAXLINE - 3);
	ll_pos = (ll_pos+1) % MAXLAST;
	UNLOCK_LL();
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


void Clastlog(const char *params, int pid, int target)
{
	int count, c, set[2] = {pid, -1};
	char *end;

	count = strtol(params, &end, 0);
	if (count < 1) count = 10;
	if (count >= MAXLAST) count = MAXLAST-1;

	if (*end)
		while (*end == ' ' || *end == '\t') end++;

	c = (ll_pos - count + MAXLAST) % MAXLAST;

	LOCK_LL();
	while (c != ll_pos)
	{
		if (ll_data[c][0])
			if (*end == '\0' || strstr(ll_data[c], end))
				chat->SendAnyMessage(set, MSG_SYSOPWARNING, 0, "%s", ll_data[c]);
		c = (c+1) % MAXLAST;
	}
	UNLOCK_LL();
}

