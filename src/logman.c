
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


typedef struct LogLine
{
	int level;
	char line[1];
} LogLine;


local void Log(char, const char *, ...);
local int FilterLog(char, const char *, const char *);
local void * LoggingThread(void *);


local MPQueue queue;
local Thread thd;

local Imodman *mm;
local Iconfig *cfg;
local Ilogman _int =
{
	INTERFACE_HEAD_INIT("logman")
	Log, FilterLog
};


EXPORT int MM_logman(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface("config", ALLARENAS);
		MPInit(&queue);
		thd = StartThread(LoggingThread, NULL);
		mm->RegInterface("logman", &_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface("logman", &_int, ALLARENAS))
			return MM_FAIL;
		MPAdd(&queue, NULL);
		JoinThread(thd);
		MPDestroy(&queue);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void * LoggingThread(void *dummy)
{
	LogLine *ll;

	for (;;)
	{
		ll = MPRemove(&queue);
		if (ll == NULL)
			return NULL;

		DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc,
				(ll->level, ll->line));
		afree(ll);
	}
}


void Log(char level, const char *format, ...)
{
	LogLine *ll;
	int len;
	va_list argptr;
	char buf[1024];

	va_start(argptr, format);
	len = vsnprintf(buf, 1024, format, argptr);
	va_end(argptr);

	if (len > 1024) len = 1024;

	if (len > 0)
	{
		ll = amalloc(len + sizeof(LogLine));
		ll->level = level;
		strcpy(ll->line, buf);
		MPAdd(&queue, ll);
	}
}


int FilterLog(char level, const char *line, const char *modname)
{
	const char *res;
	char origin[32];

	/* if there's no config manager, disable filtering */
	if (!cfg || !line || !modname)
		return TRUE;

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


