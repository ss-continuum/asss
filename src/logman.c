
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "asss.h"


local void Log(char, const char *, ...);
local void LogA(char level, const char *mod, int arena, const char *format, ...);
local void LogP(char level, const char *mod, int pid, const char *format, ...);
local int FilterLog(const char *, const char *);

local void * LoggingThread(void *);


local MPQueue queue;
local pthread_t thd;

local Imodman *mm;

/* don't load these during initialization, it would make a cycle. the
 * noinit is a hint to detect-cycles.py that these interfaces aren't
 * requested during initialization. */
local /* noinit */ Iconfig *cfg;
local /* noinit */ Iplayerdata *pd;
local /* noinit */ Iarenaman *aman;

local Ilogman _int =
{
	INTERFACE_HEAD_INIT(I_LOGMAN, "logman")
	Log, LogA, LogP, FilterLog
};


EXPORT int MM_logman(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = NULL;
		pd = NULL;
		aman = NULL;
		MPInit(&queue);
		pthread_create(&thd, NULL, LoggingThread, NULL);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(cfg); cfg = NULL;
		mm->ReleaseInterface(pd); pd = NULL;
		mm->ReleaseInterface(aman); aman = NULL;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		MPAdd(&queue, NULL);
		pthread_join(thd, NULL);
		MPDestroy(&queue);
		return MM_OK;
	}
	return MM_FAIL;
}


void * LoggingThread(void *dummy)
{
	char *ll;

	for (;;)
	{
		ll = MPRemove(&queue);
		if (ll == NULL)
			return NULL;

		DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc,
				(ll));
		afree(ll);
	}
}


void Log(char level, const char *format, ...)
{
	int len;
	va_list argptr;
	char buf[1024];

	buf[0] = level;
	buf[1] = ' ';

	va_start(argptr, format);
	len = vsnprintf(buf+2, 1022, format, argptr);
	va_end(argptr);

	if (len > 0)
		MPAdd(&queue, astrdup(buf));
}


void LogA(char level, const char *mod, int arena, const char *format, ...)
{
	int len;
	va_list argptr;
	char buf[1024];

	len = snprintf(buf, 1024, "%c <%s> {%s} ",
			level,
			mod,
			aman && ARENA_OK(arena) ? aman->arenas[arena].name : "???");
	assert(len < 1024);

	va_start(argptr, format);
	len = vsnprintf(buf + len, 1024 - len, format, argptr);
	va_end(argptr);

	if (len > 0)
		MPAdd(&queue, astrdup(buf));
}

void LogP(char level, const char *mod, int pid, const char *format, ...)
{
	int len, arena;
	va_list argptr;
	char buf[1024], buf2[16];

	if (!pd || !aman || PID_BAD(pid))
		len = snprintf(buf, 1024, "%c <%s> {???} [???] ",
				level,
				mod);
	else
	{
		char *name = pd->players[pid].name;

		if (name[0] == '\0')
		{
			name = buf2;
			snprintf(name, 16, "pid=%d", pid);
		}

		arena = pd->players[pid].arena;
		if (ARENA_OK(arena))
			len = snprintf(buf, 1024, "%c <%s> {%s} [%s] ",
					level,
					mod,
					aman->arenas[arena].name,
					name);
		else
			len = snprintf(buf, 1024, "%c <%s> [%s] ",
					level,
					mod,
					name);
	}

	assert(len < 1024);

	va_start(argptr, format);
	len = vsnprintf(buf + len, 1024 - len, format, argptr);
	va_end(argptr);

	if (len > 0)
		MPAdd(&queue, astrdup(buf));
}



int FilterLog(const char *line, const char *modname)
{
	const char *res;
	char origin[32], level;

	/* if there's no config manager, disable filtering */
	if (!cfg || !line || !modname)
		return TRUE;

	level = line[0];
	line += 2;

	if (line[0] == '<')
	{
		/* copy into origin until closing > */
		char *d = origin;
		const char *s = line+1;
		while (*s && *s != '>' && (d-origin) < 30)
			*d++ = *s++;
		*d = 0;
	}
	else
	{
		/* unknown module */
		astrncpy(origin, "unknown", 32);
	}

	res = cfg->GetStr(GLOBAL, modname, origin);
	if (!res)
	{
		/* try 'all' */
		res = cfg->GetStr(GLOBAL, modname, "all");
	}
	if (!res)
	{
		/* if no match for 'all', disable filtering */
		return TRUE;
	}
	if (strchr(res, level))
		return TRUE;
	else
		return FALSE;
}


