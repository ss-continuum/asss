
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


#define ALLOW_ALL_IF_CAPMAN_IS_MISSING 1


/* structs */
typedef struct CommandData
{
	CommandFunc func;
} CommandData;


/* prototypes */

local void AddCommand(const char *, CommandFunc);
local void RemoveCommand(const char *, CommandFunc);
local void Command(const char *, int, int);


/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Iarenaman *aman;
local Icapman *capman;

local HashTable *cmds;
local CommandFunc defaultfunc;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT("cmdman")
	AddCommand, RemoveCommand, Command
};


EXPORT int MM_cmdman(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface("playerdata", ALLARENAS);
		lm = mm->GetInterface("logman", ALLARENAS);
		aman = mm->GetInterface("arenaman", ALLARENAS);
		capman = mm->GetInterface("capman", ALLARENAS);

		cmds = HashAlloc(47);
		defaultfunc = NULL;
		mm->RegInterface("cmdman", &_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface("cmdman", &_int, ALLARENAS))
			return MM_FAIL;
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(capman);
		HashFree(cmds);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
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


void Command(const char *line, int pid, int target)
{
	LinkedList *lst;
	Link *l;
	char *saveline = (char*)line, cmd[40], *t, found = 0;

	/* first log it. this shouldn't be so complicated... */
	if (lm)
	{
		if (PID_OK(pid >= 0))
		{
			int arena = pd->players[pid].arena;
			if (ARENA_OK(arena))
				lm->Log(L_INFO, "<cmdman> {%s} [%s] Command '%s'",
						aman->arenas[arena].name,
						pd->players[pid].name,
						line);
			else
				lm->Log(L_INFO, "<cmdman> {(none)} [%s] Command '%s'",
						pd->players[pid].name,
						line);
		}
		else
			lm->Log(L_INFO, "<cmdman> Internal command '%s'",
					line);
	}

	if (!capman)
	{
#ifdef ALLOW_ALL_IF_CAPMAN_IS_MISSING
		lm->Log(L_WARN, "<cmdman> The capability manager isn't loaded, allowing all commands");
#else
		lm->Log(L_ERROR, "<cmdman> The capability manager isn't loaded, disallowing all commands");
		return;
#endif
	}

	if (target == TARGET_ARENA || target == TARGET_NONE)
		strncpy(cmd, "cmd_", 40);
	else
		strncpy(cmd, "privcmd_", 40);
	t = cmd + strlen(cmd);

	/* find end of command */
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	if (pid == PID_INTERNAL || !capman || capman->HasCapability(pid, cmd))
	{
		/* use strchr to get to the actual name again (from the cap name) */
		lst = HashGet(cmds, strchr(cmd, '_') + 1);
		for (l = LLGetHead(lst); l; l = l->next)
		{
			((CommandData*)l->data)->func(line, pid, target);
			found = 1;
		}
		LLFree(lst);
	}
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] Capability denied by capman: %s",
				pd->players[pid].name, cmd);

	if (!found && defaultfunc)
		defaultfunc(saveline, pid, target); /* give whole thing, not just params */
}



