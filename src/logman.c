
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "asss.h"



typedef struct LogLine
{
	int level;
	char line[1];
} LogLine;


local void Log(char, char *, ...);
local void * LoggingThread(void *);


local MPQueue queue;
local Thread thd;


local Imodman *mm;
local Ilogman _int = { Log };


int MM_logman(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		MPInit(&queue);
		thd = StartThread(LoggingThread, NULL);
		mm->RegInterface(I_LOGMAN, &_int);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		MPAdd(&queue, NULL);
		JoinThread(thd);
		MPDestroy(&queue);
		mm->UnregInterface(I_LOGMAN, &_int);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void * LoggingThread(void *dummy)
{
	LogLine *ll;
	LinkedList *lst;
	Link *l;

	for (;;)
	{
		ll = MPRemove(&queue);
		if (ll == NULL)
			return NULL;

		lst = mm->LookupCallback(CALLBACK_LOGFUNC, ALLARENAS);
		for (l = LLGetHead(lst); l; l = l->next)
			((LogFunc)(l->data))(ll->level, ll->line);
		mm->FreeLookupResult(lst);
		afree(ll);
	}
}


void Log(char level, char *format, ...)
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



