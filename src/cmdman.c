
#include <stdio.h>
#include <stdlib.h>

#include "asss.h"


/* structs */
typedef struct CommandData
{
	CommandFunc func;
	int oplevel;
} CommandData;


/* prototypes */

local void AddCommand(const char *, CommandFunc, int);
local void RemoveCommand(const char *, CommandFunc);
local void Command(const char *, int, int);


/* static data */
local Iplayerdata *pd;
local Ilogman *log;
local Iarenaman *aman;

local HashTable *cmds;
local CommandFunc defaultfunc;

local Icmdman _int = { AddCommand, RemoveCommand, Command };


int MM_cmdman(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_ARENAMAN, &aman);

		cmds = HashAlloc(47);
		defaultfunc = NULL;
		mm->RegInterface(I_CMDMAN, &_int);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CMDMAN, &_int);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
		HashFree(cmds);
		return MM_OK;
	}
	return MM_FAIL;
}



void AddCommand(const char *cmd, CommandFunc f, int oplevel)
{
	if (!cmd)
		defaultfunc = f;
	else
	{
		CommandData *data;
		data = amalloc(sizeof(CommandData));
		data->func = f;
		data->oplevel = oplevel;
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
	CommandData *data;
	char *saveline = (char*)line, cmd[32], *t = cmd, found = 0;
	int opl;

	/* first log it. this shouldn't be so complicated... */
	if (log)
	{
		if (pid >= 0 && pid < MAXPLAYERS)
		{
			int arena = pd->players[pid].arena;
			if (arena >= 0 && arena < MAXARENA)
				log->Log(L_INFO, "<cmdman> {%s} [%s] Command '%s'",
						aman->arenas[arena].name,
						pd->players[pid].name,
						line);
			else
				log->Log(L_INFO, "<cmdman> {(none)} [%s] Command '%s'",
						pd->players[pid].name,
						line);
		}
		else
			log->Log(L_INFO, "<cmdman> Internal command '%s'",
					line);
	}

	/* find end of command */
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	if (pid >= 0 && pid < MAXPLAYERS)
	{
		pd->LockPlayer(pid);
		opl = pd->players[pid].oplevel;
		pd->UnlockPlayer(pid);
	}

	lst = HashGet(cmds, cmd);
	for (l = LLGetHead(lst); l; l = l->next)
	{
		int runme = 0;

		data = (CommandData*) l->data;

		if (pid < 0)
			runme = 1;
		else if (pid < MAXPLAYERS)
			if (opl >= data->oplevel)
				runme = 1;
		if (runme)
			data->func(line, pid, target);

		found = 1;
	}
	LLFree(lst);

	if (!found && defaultfunc)
		defaultfunc(saveline, pid, target); /* give whole thing, not just params */
}



