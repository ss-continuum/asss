
#include <stdio.h>
#include <time.h>

#include "asss.h"


#define TIMEFORMAT "%b %d %H:%M:%S"
#define TIMEFORMATLEN 20


local void LogFile(char, char *);
local void FlushLog(void);
local void ReopenLog(void);

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
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void LogFile(char lev, char *s)
{
	pthread_mutex_lock(&logmtx);

	if (logfile)
	{
		time_t t1;
		char t3[TIMEFORMATLEN];

		time(&t1);
		strftime(t3, TIMEFORMATLEN, TIMEFORMAT, localtime(&t1));
		fprintf(logfile, "%s %c %s\n", t3, lev, s);
	}

	pthread_mutex_unlock(&logmtx);
}

void FlushLog(void)
{
	pthread_mutex_lock(&logmtx);
	if (logfile) fflush(logfile);
	pthread_mutex_unlock(&logmtx);
}

void ReopenLog(void)
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

