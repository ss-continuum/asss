
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


/* structs */
typedef struct CommandData
{
	CommandFunc func;
} CommandData;


/* prototypes */

local void AddCommand(const char *, CommandFunc);
local void RemoveCommand(const char *, CommandFunc);
local void Command(const char *, int, const Target *);

/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Icapman *capman;
local Iconfig *cfg;

local HashTable *cmds;
local CommandFunc defaultfunc;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT(I_CMDMAN, "cmdman")
	AddCommand, RemoveCommand, Command
};


EXPORT int MM_cmdman(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		cmds = HashAlloc(47);

		defaultfunc = NULL;
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(cfg);
		HashFree(cmds);
		return MM_OK;
	}
	return MM_FAIL;
}


void AddCommand(const char *cmd, CommandFunc f)
{
	if (!cmd)
		defaultfunc = f;
	else
	{
		CommandData *data;
		data = amalloc(sizeof(CommandData));
		data->func = f;
		HashAdd(cmds, cmd, data);
	}
}


void RemoveCommand(const char *cmd, CommandFunc f)
{
	if (!cmd)
	{
		if (defaultfunc == f)
			f = NULL;
	}
	else
	{
		CommandData *data;
		LinkedList *lst;
		Link *l;

		lst = HashGet(cmds, cmd);
		for (l = LLGetHead(lst); l; l = l->next)
		{
			data = (CommandData*) l->data;
			if (data->func == f)
			{
				HashRemove(cmds, cmd, data);
				LLFree(lst);
				afree(data);
				return;
			}
		}
		LLFree(lst);
	}
}


local void log_command(int pid, const Target *target, const char *cmd, const char *params)
{
	char t[32];

	if (!lm)
		return;

	/* don't log the params to some commands */
	if (cfg->GetStr(GLOBAL, "DontLogParams", cmd))
		params = "...";

	if (target->type == T_ARENA)
		astrncpy(t, "(arena)", 32);
	else if (target->type == T_FREQ)
		snprintf(t, 32, "(freq %d)", target->u.freq.freq);
	else if (target->type == T_PID)
		snprintf(t, 32, "to [%s]", pd->players[target->u.pid].name);
	else
		astrncpy(t, "(other)", 32);

	if (*params)
		lm->LogP(L_INFO, "cmdman", pid, "Command %s '%s' '%s'",
				t, cmd, params);
	else
		lm->LogP(L_INFO, "cmdman", pid, "Command %s '%s'",
				t, cmd);
}


void Command(const char *line, int pid, const Target *target)
{
	LinkedList *lst;
	Link *l;
	char *saveline = (char*)line, cmd[40], *t, found = 0;

	if (!capman)
	{
#ifdef ALLOW_ALL_IF_CAPMAN_IS_MISSING
		lm->Log(L_WARN, "<cmdman> The capability manager isn't loaded, allowing all commands");
#else
		lm->Log(L_WARN, "<cmdman> The capability manager isn't loaded, disallowing all commands");
		return;
#endif
	}

	if (target->type == T_ARENA || target->type == T_NONE)
		astrncpy(cmd, "cmd_", 40);
	else
		astrncpy(cmd, "privcmd_", 40);
	t = cmd + strlen(cmd);

	/* find end of command */
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	if (!capman || capman->HasCapability(pid, cmd))
	{
		/* use strchr to get to the actual name again (from the cap name) */
		char *cmdname = strchr(cmd, '_') + 1;
		lst = HashGet(cmds, cmdname);
		for (l = LLGetHead(lst); l; l = l->next)
		{
			((CommandData*)l->data)->func(line, pid, target);
			found = 1;
		}
		LLFree(lst);
		log_command(pid, target, cmdname, line);
	}
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] Capability denied by capman: %s",
				pd->players[pid].name, cmd);

	if (!found && defaultfunc)
		defaultfunc(saveline, pid, target); /* give whole thing, not just params */
}



