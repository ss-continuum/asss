
#include <stdio.h>

#include "asss.h"


local void LogConsole(char, char *);


EXPORT int MM_log_console(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm->RegCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void LogConsole(char lev, char *s)
{
	puts(s);
}

