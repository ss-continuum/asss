
#include <stdio.h>
#include <time.h>

#include "asss.h"


local void LogFile(const char *);
local void FlushLog(void);
local void ReopenLog(void);

local FILE *logfile;
local pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

local Iconfig *cfg;
local Ilogman *lm;

local Ilog_file _lfint =
{
	INTERFACE_HEAD_INIT(I_LOG_FILE, "log_file")
	FlushLog, ReopenLog
};


EXPORT int MM_log_file(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!cfg || !lm) return MM_FAIL;

		logfile = NULL;
		ReopenLog();

		mm->RegCallback(CB_LOGFUNC, LogFile, ALLARENAS);

		mm->RegInterface(&_lfint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (logfile) fclose(logfile);
		if (mm->UnregInterface(&_lfint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_LOGFUNC, LogFile, ALLARENAS);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogFile(const char *s)
{
	if (lm->FilterLog(s, "log_file"))
	{
		pthread_mutex_lock(&logmtx);
		if (logfile)
		{
			time_t t1;
			char t3[CFG_TIMEFORMATLEN];

			time(&t1);
			strftime(t3, CFG_TIMEFORMATLEN, CFG_TIMEFORMAT, localtime(&t1));
			fputs(t3, logfile);
			putc(' ', logfile);
			fputs(s, logfile);
			putc('\n', logfile);
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

