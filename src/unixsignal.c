
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include "asss.h"
#include "log_file.h"
#include "persist.h"


local Imodman *mm;

local volatile sig_atomic_t gotsig;


local void handle_sighup(void)
{
	Ilog_file *lf = mm->GetInterface(I_LOG_FILE, ALLARENAS);
	Iconfig *cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
	Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
	if (lf)
		lf->ReopenLog();
	if (cfg)
	{
		cfg->FlushDirtyValues();
		cfg->CheckModifiedFiles();
	}
	if (persist)
		persist->StabilizeScores(0, 0, NULL);
	mm->ReleaseInterface(lf);
	mm->ReleaseInterface(cfg);
	mm->ReleaseInterface(persist);
}

local void handle_sigint(void)
{
	Imainloop *ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
	if (ml)
		ml->Quit(EXIT_NONE);
	mm->ReleaseInterface(ml);
}

local void handle_sigterm(void)
{
	handle_sigint();
}

local void handle_sigusr1(void)
{
	Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
	persist->StabilizeScores(5, 0, NULL);
	mm->ReleaseInterface(persist);
}

local void handle_sigusr2(void)
{
	char buf[256];
	Ichat *chat;
	FILE *f = fopen("MESSAGE", "r");

	if (f)
	{
		if (fgets(buf, sizeof(buf), f) && (chat = mm->GetInterface(I_CHAT, ALLARENAS)))
		{
			RemoveCRLF(buf);
			chat->SendArenaMessage(ALLARENAS, "%s", buf);
			mm->ReleaseInterface(chat);
		}
		fclose(f);
		unlink("MESSAGE");
	}
}


local void check_signals(void)
{
	switch (gotsig)
	{
		case SIGHUP:  handle_sighup();  break;
		case SIGINT:  handle_sigint();  break;
		case SIGTERM: handle_sigterm(); break;
		case SIGUSR1: handle_sigusr1(); break;
		case SIGUSR2: handle_sigusr2(); break;
	}
	gotsig = 0;
}


local void sigfunc(int sig)
{
	gotsig = sig;
}

local void init_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = sigfunc;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
}

local void deinit_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
}

local void write_pid(const char *fn)
{
	FILE *f = fopen(fn, "w");
	fprintf(f, "%d\n", getpid());
	fclose(f);
}


EXPORT int MM_unixsignal(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegCallback(CB_MAINLOOP, check_signals, ALLARENAS);
		init_signals();
		if (CFG_PID_FILE)
			write_pid(CFG_PID_FILE);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		deinit_signals();
		mm->UnregCallback(CB_MAINLOOP, check_signals, ALLARENAS);
		if (CFG_PID_FILE)
			unlink(CFG_PID_FILE);
		return MM_OK;
	}
	else
		return MM_FAIL;
}

