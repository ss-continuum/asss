
#include <stdio.h>

#include "asss.h"



local void LogFile(int, char *);


local FILE *logfile;
Iconfig *cfg;


int MM_log_file(int action, Imodman *mm, int arenas)
{
	char fname[64], *ln;

	if (action == MM_LOAD)
	{
		mm->RegInterest(I_CONFIG, &cfg);

		if (!log || !cfg) return MM_FAIL;

		ln = cfg->GetStr(GLOBAL,"Log","LogFile");
		if (!ln) ln = "asss.log";
		sprintf(fname, "log/%s", ln);
		logfile = fopen(fname, "a");
		if (!logfile) return MM_FAIL;
		mm->RegCallback(CALLBACK_LOGFUNC, LogFile, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CALLBACK_LOGFUNC, LogFile, ALLARENAS);
		fclose(logfile);
		mm->UnregInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogFile(int lev, char *s)
{
	fputs(s, logfile);
	fputc('\n', logfile);
}


