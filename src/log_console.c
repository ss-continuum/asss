
#include <stdio.h>

#include "asss.h"


local void LogConsole(char *);

local Ilogman *lm;


EXPORT int MM_log_console(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!lm) return MM_FAIL;
		mm->RegCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogConsole(char *s)
{
	if (lm->FilterLog(s, "log_console"))
		puts(s);
}

