
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


/* structs */
typedef struct cmddata_t
{
	CommandFunc func;
	CommandFunc2 func2;
	Arena *arena;
	helptext_t helptext;
} cmddata_t;


/* prototypes */

local void AddCommand(const char *, CommandFunc, helptext_t);
local void RemoveCommand(const char *, CommandFunc);
local void AddCommand2(const char *, CommandFunc2, Arena *, helptext_t);
local void RemoveCommand2(const char *, CommandFunc2, Arena *);
local void Command(const char *, Player *, const Target *);
local helptext_t GetHelpText(const char *, Arena *);

/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Icapman *capman;
local Iconfig *cfg;
local Imodman *mm;

local pthread_mutex_t cmdmtx;
local HashTable *cmds;
local CommandFunc defaultfunc;
local CommandFunc2 defaultfunc2;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT(I_CMDMAN, "cmdman")
	AddCommand, AddCommand2,
	RemoveCommand, RemoveCommand2,
	Command, GetHelpText
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

		cmds = HashAlloc();

		defaultfunc = NULL;
		defaultfunc2 = NULL;

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
		cmddata_t *data = amalloc(sizeof(*data));
		data->func = f;
		data->func2 = NULL;
		data->arena = ALLARENAS;
		data->helptext = helptext;
		pthread_mutex_lock(&cmdmtx);
		HashAdd(cmds, cmd, data);
		pthread_mutex_unlock(&cmdmtx);
	}
}


void AddCommand2(const char *cmd, CommandFunc2 f2, Arena *arena,
		helptext_t helptext)
{
	if (!cmd)
		defaultfunc2 = f2;
	else
	{
		cmddata_t *data = amalloc(sizeof(*data));
		data->func = NULL;
		data->func2 = f2;
		data->arena = arena;
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
		LinkedList lst = LL_INITIALIZER;
		Link *l;

		pthread_mutex_lock(&cmdmtx);
		HashGetAppend(cmds, cmd, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			cmddata_t *data = l->data;
			if (data->func == f && data->arena == ALLARENAS)
			{
				HashRemove(cmds, cmd, data);
				pthread_mutex_unlock(&cmdmtx);
				LLEmpty(&lst);
				afree(data);
				return;
			}
		}
		pthread_mutex_unlock(&cmdmtx);
		LLEmpty(&lst);
	}
}


void RemoveCommand2(const char *cmd, CommandFunc2 f2, Arena *arena)
{
	if (!cmd)
	{
		if (defaultfunc2 == f2)
			defaultfunc2 = NULL;
	}
	else
	{
		LinkedList lst = LL_INITIALIZER;
		Link *l;

		pthread_mutex_lock(&cmdmtx);
		HashGetAppend(cmds, cmd, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			cmddata_t *data = l->data;
			if (data->func2 == f2 && data->arena == arena)
			{
				HashRemove(cmds, cmd, data);
				pthread_mutex_unlock(&cmdmtx);
				LLEmpty(&lst);
				afree(data);
				return;
			}
		}
		pthread_mutex_unlock(&cmdmtx);
		LLEmpty(&lst);
	}
}



local inline int dontlog(const char *cmd)
{
	if (!strcasecmp(cmd, "chat")) return TRUE;
	if (!strcasecmp(cmd, "password")) return TRUE;
	if (!strcasecmp(cmd, "passwd")) return TRUE;
	if (!strcasecmp(cmd, "squadcreate")) return TRUE;
	if (!strcasecmp(cmd, "squadjoin")) return TRUE;
	if (!strcasecmp(cmd, "addop")) return TRUE;
	if (!strcasecmp(cmd, "adduser")) return TRUE;
	if (!strcasecmp(cmd, "changepassword")) return TRUE;
	if (!strcasecmp(cmd, "login")) return TRUE;
	if (!strcasecmp(cmd, "blogin")) return TRUE;
	if (!strcasecmp(cmd, "bpassword")) return TRUE;
	return FALSE;
}


local void log_command(Player *p, const Target *target, const char *cmd, const char *params)
{
	char t[32];

	if (!lm) return;

	/* don't log the params to some commands */
	if (dontlog(cmd))
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
		lm->LogP(L_INFO, "cmdman", p, "command %s: %s %s",
				t, cmd, params);
	else
		lm->LogP(L_INFO, "cmdman", p, "command %s: %s",
				t, cmd);
}


local int allowed(Player *p, const char *cmd, int private)
{
	char cap[40];
	
	if (!capman)
	{
#ifdef ALLOW_ALL_IF_CAPMAN_IS_MISSING
		lm->Log(L_WARN, "<cmdman> the capability manager isn't loaded, allowing all commands");
		return TRUE;
#else
		lm->Log(L_WARN, "<cmdman> the capability manager isn't loaded, disallowing all commands");
		return FALSE;
#endif
	}

	snprintf(cap, sizeof(cap), "privcmd_%s", cmd);
	return capman->HasCapability(p, private ? cap : cap + 4);
}


void Command(const char *line, Player *p, const Target *target)
{
	LinkedList lst = LL_INITIALIZER;
	char cmd[40], *t;
	int private, skiplocal = FALSE;
	const char *origline;

	/* almost all commands assume p->arena is non-null */
	if (p->arena == NULL)
		return;

	if (line[0] == '\\')
	{
		line++;
		skiplocal = TRUE;
	}
	origline = line;

	/* find end of command */
	t = cmd;
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off */
	*t = 0;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	private = target->type != T_ARENA && target->type != T_NONE;

	pthread_mutex_lock(&cmdmtx);
	HashGetAppend(cmds, cmd, &lst);

	if (skiplocal || LLIsEmpty(&lst))
	{
		/* we don't know about this, send it to the biller */
		if (defaultfunc2)
			defaultfunc2(cmd, line, p, target);
		else if (defaultfunc)
			defaultfunc(origline, p, target);
	}
	else if (allowed(p, cmd, private))
	{
		Link *l;
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			cmddata_t *data = l->data;
			if (data->arena != ALLARENAS && data->arena != p->arena)
				continue;
			else if (data->func)
				data->func(line, p, target);
			else if (data->func2)
				data->func2(cmd, line, p, target);
		}
		log_command(p, target, cmd, line);
	}
#ifdef CFG_LOG_ALL_COMMAND_DENIALS
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] permission denied for %s",
				p->name, cmd);
#endif

	pthread_mutex_unlock(&cmdmtx);
	LLEmpty(&lst);
}


helptext_t GetHelpText(const char *cmd, Arena *a)
{
	helptext_t ret = NULL;
	LinkedList lst = LL_INITIALIZER;
	Link *l;

	pthread_mutex_lock(&cmdmtx);
	HashGetAppend(cmds, cmd, &lst);
	for (l = LLGetHead(&lst); l; l = l->next)
	{
		cmddata_t *cd = l->data;
		if (cd->arena == ALLARENAS || cd->arena == a)
			ret = cd->helptext;
	}
	pthread_mutex_unlock(&cmdmtx);

	return ret;
}

