
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


/* structs */
typedef struct CommandData
{
	CommandFunc func;
	helptext_t helptext;
} CommandData;


/* prototypes */

local void AddCommand(const char *, CommandFunc, helptext_t helptext);
local void RemoveCommand(const char *, CommandFunc);
local void Command(const char *, int, const Target *);

local void Chelp(const char *, int, const Target *);
local helptext_t help_help;

/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Icapman *capman;
local Iconfig *cfg;
local Imodman *mm;

local HashTable *cmds;
local CommandFunc defaultfunc;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT(I_CMDMAN, "cmdman")
	AddCommand, RemoveCommand, Command
};


EXPORT int MM_cmdman(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		cmds = HashAlloc(47);

		defaultfunc = NULL;

		if (CFG_HELP_COMMAND)
			AddCommand(CFG_HELP_COMMAND, Chelp, help_help);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		if (CFG_HELP_COMMAND)
			RemoveCommand(CFG_HELP_COMMAND, Chelp);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(cfg);
		HashFree(cmds);
		return MM_OK;
	}
	return MM_FAIL;
}


void AddCommand(const char *cmd, CommandFunc f, helptext_t helptext)
{
	if (!cmd)
		defaultfunc = f;
	else
	{
		CommandData *data;
		data = amalloc(sizeof(CommandData));
		data->func = f;
		data->helptext = helptext;
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


enum
{
	check_private,
	check_public,
	check_either
};

local int allowed(int pid, const char *cmd, int check)
{
	char cap[40];
	
	if (!capman)
	{
#ifdef ALLOW_ALL_IF_CAPMAN_IS_MISSING
		lm->Log(L_WARN, "<cmdman> The capability manager isn't loaded, allowing all commands");
		return TRUE;
#else
		lm->Log(L_WARN, "<cmdman> The capability manager isn't loaded, disallowing all commands");
		return FALSE;
#endif
	}

	if (check == check_private)
	{
		strcpy(cap, "privcmd_");
		strncat(cap, cmd, 30);
		return capman->HasCapability(pid, cap);
	}
	else if (check == check_public)
	{
		strcpy(cap, "cmd_");
		strncat(cap, cmd, 30);
		return capman->HasCapability(pid, cap);
	}
	else if (check == check_either)
	{
		strcpy(cap, "privcmd_");
		strncat(cap, cmd, 30);
		return capman->HasCapability(pid, cap) ||
		       capman->HasCapability(pid, cap+4);
	}
	else
		return FALSE;
}


local helptext_t help_help =
"Targets: none\n"
"Args: <command name>\n"
"Displays help on other commands.";

void Chelp(const char *params, int pid, const Target *target)
{
	LinkedList *lst;
	Link *l;
	Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);

	if (!chat) return;

	if (params[0] == '?' || params[0] == '*' || params[0] == '!')
		params++;

	if (params[0] == '\0')
		params = CFG_HELP_COMMAND;

	lst = HashGet(cmds, params);

	if (LLIsEmpty(lst))
		chat->SendMessage(pid, "I don't know anything about '?%s'", params);
	else if (allowed(pid, params, check_either))
		for (l = LLGetHead(lst); l; l = l->next)
		{
			char buf[256], *t;
			const char *temp = NULL;
			helptext_t ht = ((CommandData*)(l->data))->helptext;
			if (ht)
			{
				chat->SendMessage(pid, "Help on '?%s':", params);
				while (strsplit(ht, "\n", buf, 256, &temp))
				{
					for (t = buf; *t; t++)
						if (*t == '{' || *t == '}')
							*t = '\'';
					chat->SendMessage(pid, "  %s", buf);
				}
			}
			else
				chat->SendMessage(pid, "There is no help for '?%s'", params);
		}
	else
		chat->SendMessage(pid, "You don't have permission to use '?%s'", params);

	LLFree(lst);
	mm->ReleaseInterface(chat);
}



void Command(const char *line, int pid, const Target *target)
{
	LinkedList *lst;
	Link *l;
	const char *saveline = line;
	char cmd[40], *t, found = 0;
	int check;

	/* find end of command */
	t = cmd;
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	if (target->type == T_ARENA || target->type == T_NONE)
		check = check_public;
	else
		check = check_private;

	if (allowed(pid, cmd, check))
	{
		lst = HashGet(cmds, cmd);
		for (l = LLGetHead(lst); l; l = l->next)
		{
			((CommandData*)l->data)->func(line, pid, target);
			found = 1;
		}
		LLFree(lst);
		log_command(pid, target, cmd, line);
	}
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] Permission denied for %s",
				pd->players[pid].name, cmd);

	if (!found && defaultfunc)
		defaultfunc(saveline, pid, target); /* give whole thing, not just params */
}



