
#include <stdio.h>

#include "asss.h"



local void LogFile(int, char *);


local FILE *logfile;


int MM_log_file(int action, Imodman *mm)
{
	char fname[64], *ln;
	Ilogman *log;
	Iconfig *cfg;

	if (action == MM_LOAD)
	{
		log = mm->GetInterface(I_LOGMAN);
		cfg = mm->GetInterface(I_CONFIG);
		if (!log || !cfg) return MM_FAIL;
		ln = cfg->GetStr(GLOBAL,"Log","LogFile");
		if (!ln) ln = "asss.log";
		sprintf(fname, "log/%s", ln);
		logfile = fopen(fname, "a");
		if (!logfile) return MM_FAIL;
		mm->RegCallback(CALLBACK_LOGFUNC, LogFile);
	}
	else if (action == MM_UNLOAD)
	{
		log = mm->GetInterface(I_LOGMAN);
		mm->UnregCallback(CALLBACK_LOGFUNC, LogFile);
		fclose(logfile);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "log_file - logs output to a file";
	}
	return MM_OK;
}


void LogFile(int lev, char *s)
{
	fputs(s, logfile);
	fputc('\n', logfile);
}


