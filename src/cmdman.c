
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

local Icmdman _int = { AddCommand, RemoveCommand, Command };

local HashTable *cmds;
local CommandFunc defaultfunc;
local PlayerData *players;


int MM_cmdman(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		cmds = HashAlloc(47);
		defaultfunc = NULL;
		mm->RegInterface(I_CMDMAN, &_int);
		players = mm->players;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CMDMAN, &_int);
		HashFree(cmds);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "cmdman - manages typed commands for all modules";
	}
	return MM_OK;

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

	/* find end of command */
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	lst = HashGet(cmds, cmd);
	for (l = LLGetHead(lst); l; l = l->next)
	{
		data = (CommandData*) l->data;
		if (pid < 0 || (pid < MAXPLAYERS && players[pid].oplevel >= data->oplevel))
			data->func(line, pid, target);
		found = 1;
	}
	LLFree(lst);

	if (!found && defaultfunc)
		defaultfunc(saveline, pid, target); /* give whole thing, not just params */
}



