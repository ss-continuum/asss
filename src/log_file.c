
#include <stdio.h>

#include "asss.h"


local void LogFile(int, char *);
local void FlushLog();
local void ReopenLog();

local FILE *logfile;
local pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

local Iconfig *cfg;

local Ilog_file _lfint = { FlushLog, ReopenLog };


int MM_log_file(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_CONFIG, &cfg);

		if (!cfg) return MM_FAIL;

		logfile = NULL;
		ReopenLog();

		mm->RegCallback(CALLBACK_LOGFUNC, LogFile, ALLARENAS);

		mm->RegInterface(I_LOG_FILE, &_lfint);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (logfile) fclose(logfile);
		mm->UnregInterface(I_LOG_FILE, &_lfint);
		mm->UnregCallback(CALLBACK_LOGFUNC, LogFile, ALLARENAS);
		mm->UnregInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogFile(int lev, char *s)
{
	pthread_mutex_lock(&logmtx);

	if (logfile)
	{
		fputs(s, logfile);
		fputc('\n', logfile);
	}

	pthread_mutex_unlock(&logmtx);
}

void FlushLog()
{
	pthread_mutex_lock(&logmtx);
	if (logfile) fflush(logfile);
	pthread_mutex_unlock(&logmtx);
}

void ReopenLog()
{
	char *ln, fname[256];

	pthread_mutex_lock(&logmtx);

	if (logfile)
		fclose(logfile);

	ln = cfg->GetStr(GLOBAL,"Log","LogFile");
	if (!ln) ln = "asss.log";

	sprintf(fname, "log/%s", ln);
	logfile = fopen(fname, "a");

	pthread_mutex_unlock(&logmtx);
}

