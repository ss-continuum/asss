
#include <stdio.h>

#include "asss.h"


local void LogConsole(int, char *);


int MM_log_console(int action, Imodman *mm)
{
	Ilogman *log;

	if (action == MM_LOAD)
	{
		log = mm->GetInterface(I_LOGMAN);
		if (!log) return MM_FAIL;
		log->AddLog(LogConsole);
	}
	else if (action == MM_UNLOAD)
	{
		log = mm->GetInterface(I_LOGMAN);
		if (!log) return MM_FAIL;
		log->RemoveLog(LogConsole);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "log_console - logs output to stdout";
	}
	return MM_OK;
}


void LogConsole(int lev, char *s)
{
	puts(s);
}

