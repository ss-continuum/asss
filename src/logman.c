
#include <stdarg.h>
#include <stdio.h>

#include "asss.h"



local void AddLog(LogFunc);
local void RemoveLog(LogFunc);
local void Log(int, char *, ...);




local Ilogman _int = { AddLog, RemoveLog, Log };

local LinkedList *funcs;


int MM_logman(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		funcs = LLAlloc();
		mm->RegisterInterface(I_LOGMAN, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_int);
		LLFree(funcs);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "log_logman - formats log entries and passes them to a managed "
					"set of logging modules";
	}
	return MM_OK;
}


void AddLog(LogFunc f)
{
	LLAdd(funcs, f);
}

void RemoveLog(LogFunc f)
{
	LLRemove(funcs, f);
}

void Log(int level, char *format, ...)
{
	LogFunc f;
	va_list argptr;
	static char buf[1024];
	
	va_start(argptr, format);
	vsprintf(buf, format, argptr);
	va_end(argptr);

	LLRewind(funcs);
	while ((f = LLNext(funcs)))
		f(level, buf);
}



