

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <paths.h>
#include <unistd.h>
#include <fcntl.h>


#include "asss.h"




local Imodman *mm;
local int dodaemonize;



local void ProcessArgs(int argc, char *argv[])
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--daemonize"))
			dodaemonize = 1;
	}
}


local void LoadModuleFile(char *fname)
{
	static char line[256];
	int ret;
	FILE *f;

	f = fopen(fname,"r");

	if (!f)
		Error(ERROR_MODCONF, "Couldn't open '%s'", fname);

	while (fgets(line, 256, f))
	{
		RemoveCRLF(line);
		if (line[0] && line[0] != ';' && line[0] != '#' && line[0] != '/')
		{
			ret = mm->LoadModule(line);
			if (ret == MM_FAIL)
				Error(ERROR_MODLOAD, "Error in loading module '%s'", line);
		}
	}

	fclose(f);
}


local int daemonize(int noclose)
{
	int fd;

	switch (fork())
	{
		case -1:
			return -1;
		case 0:
			break;
		default:
			_exit(0);
	}

	if (setsid() == -1) return -1;
	if (noclose) return 0;

	fd = open(_PATH_DEVNULL, O_RDWR, 0);
	if (fd != -1)
	{
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2) close(fd);
	}
	return 0;
}


int main(int argc, char *argv[])
{
	Ilogman *log;
	Imainloop *ml;

	ProcessArgs(argc,argv);

	mm = InitModuleManager();

	printf("asss 0.5 (buildnumber %d)\n", BUILDNUMBER);

	if (dodaemonize)
		daemonize(0);

	printf("Loading modules...\n");

	LoadModuleFile("conf/modules.conf");

	mm->RegInterest(I_LOGMAN, &log);
	mm->RegInterest(I_MAINLOOP, &ml);

	if (!ml)
		Error(ERROR_MODLOAD, "mainloop module missing");

	if (log) log->Log(L_DRIVEL,"<main> Entering main loop");

	ml->RunLoop();

	if (log) log->Log(L_DRIVEL,"<main> Exiting main loop");

	mm->UnregInterest(I_LOGMAN, &log);
	mm->UnregInterest(I_MAINLOOP, &ml);

	mm->UnloadAllModules();

	return 0;
}


