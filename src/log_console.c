
#include <stdio.h>

#include "asss.h"


local void LogConsole(int, char *);


local Ilogman *log;


int MM_log_console(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegCallback(CALLBACK_LOGFUNC, LogConsole);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CALLBACK_LOGFUNC, LogConsole);
		mm->UnregInterface(I_LOGMAN, &log);
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

