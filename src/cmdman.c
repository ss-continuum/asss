
/* dist: public */

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
local void Command(const char *, Player *, const Target *);
local helptext_t GetHelpText(const char *);

/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Icapman *capman;
local Iconfig *cfg;
local Imodman *mm;

local pthread_mutex_t cmdmtx;
local HashTable *cmds;
local CommandFunc defaultfunc;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT(I_CMDMAN, "cmdman")
	AddCommand, RemoveCommand, Command, GetHelpText
};


EXPORT int MM_cmdman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&cmdmtx, &attr);
		pthread_mutexattr_destroy(&attr);

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
		pthread_mutex_destroy(&cmdmtx);
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
		CommandData *data = amalloc(sizeof(*data));
		data->func = f;
		data->helptext = helptext;
		pthread_mutex_lock(&cmdmtx);
		HashAdd(cmds, cmd, data);
		pthread_mutex_unlock(&cmdmtx);
	}
}


void RemoveCommand(const char *cmd, CommandFunc f)
{
	if (!cmd)
	{
		if (defaultfunc == f)
			defaultfunc = NULL;
	}
	else
	{
		CommandData *data;
		LinkedList lst = LL_INITIALIZER;
		Link *l;

		pthread_mutex_lock(&cmdmtx);
		HashGetAppend(cmds, cmd, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			data = (CommandData*) l->data;
			if (data->func == f)
			{
				HashRemove(cmds, cmd, data);
				LLEmpty(&lst);
				afree(data);
				pthread_mutex_unlock(&cmdmtx);
				return;
			}
		}
		LLEmpty(&lst);
		pthread_mutex_unlock(&cmdmtx);
	}
}


local void log_command(Player *p, const Target *target, const char *cmd, const char *params)
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
	else if (target->type == T_PLAYER)
		snprintf(t, 32, "to [%s]", target->u.p->name);
	else
		astrncpy(t, "(other)", 32);

	if (*params)
		lm->LogP(L_INFO, "cmdman", p, "Command %s '%s' '%s'",
				t, cmd, params);
	else
		lm->LogP(L_INFO, "cmdman", p, "Command %s '%s'",
				t, cmd);
}


enum
{
	check_private,
	check_public,
	check_either
};

local int allowed(Player *p, const char *cmd, int check)
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
		return capman->HasCapability(p, cap);
	}
	else if (check == check_public)
	{
		strcpy(cap, "cmd_");
		strncat(cap, cmd, 30);
		return capman->HasCapability(p, cap);
	}
	else if (check == check_either)
	{
		strcpy(cap, "privcmd_");
		strncat(cap, cmd, 30);
		return capman->HasCapability(p, cap) ||
		       capman->HasCapability(p, cap+4);
	}
	else
		return FALSE;
}


void Command(const char *line, Player *p, const Target *target)
{
	LinkedList lst = LL_INITIALIZER;
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

	if (allowed(p, cmd, check))
	{
		pthread_mutex_lock(&cmdmtx);
		HashGetAppend(cmds, cmd, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			((CommandData*)l->data)->func(line, p, target);
			found = 1;
		}
		LLEmpty(&lst);
		pthread_mutex_unlock(&cmdmtx);
		log_command(p, target, cmd, line);
	}
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] Permission denied for %s",
				p->name, cmd);

	if (!found && defaultfunc)
		defaultfunc(saveline, p, target); /* give whole thing, not just params */
}


helptext_t GetHelpText(const char *cmd)
{
	CommandData *cd;
	helptext_t ret;

	pthread_mutex_lock(&cmdmtx);
	cd = HashGetOne(cmds, cmd);
	ret = cd ? cd->helptext : NULL;
	pthread_mutex_unlock(&cmdmtx);

	return ret;
}

