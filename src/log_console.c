
#include <stdio.h>

#include "asss.h"


local void LogConsole(int, char *);


int MM_log_console(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm->RegCallback(CALLBACK_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CALLBACK_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogConsole(int lev, char *s)
{
	puts(s);
}

