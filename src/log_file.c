
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
local Ilogman *log;

local Ilog_file _lfint =
{
	INTERFACE_HEAD_INIT("log-file")
	FlushLog, ReopenLog
};


EXPORT int MM_log_file(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface("config", ALLARENAS);
		log = mm->GetInterface("logman", ALLARENAS);

		if (!cfg || !log) return MM_FAIL;

		logfile = NULL;
		ReopenLog();

		mm->RegCallback(CB_LOGFUNC, LogFile, ALLARENAS);

		mm->RegInterface("log_file", &_lfint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (logfile) fclose(logfile);
		if (mm->UnregInterface("log_file", &_lfint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_LOGFUNC, LogFile, ALLARENAS);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(log);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void LogFile(char lev, char *s)
{
	if (log->FilterLog(lev, s, "log_file"))
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
}

void FlushLog(void)
{
	pthread_mutex_lock(&logmtx);
	if (logfile) fflush(logfile);
	pthread_mutex_unlock(&logmtx);
}

void ReopenLog(void)
{
	const char *ln;
	char fname[256];

	pthread_mutex_lock(&logmtx);

	if (logfile)
		fclose(logfile);

	ln = cfg->GetStr(GLOBAL,"Log","LogFile");
	if (!ln) ln = "asss.log";

	sprintf(fname, "log/%s", ln);
	logfile = fopen(fname, "a");

	pthread_mutex_unlock(&logmtx);
}

