
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <paths.h>
#include <unistd.h>
#include <fcntl.h>
#endif

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


local void CheckBin(const char *argv0)
{
#ifndef WIN32
	struct stat st;
	char binpath[PATH_MAX], *t;

	if (stat("bin", &st) == -1)
	{
		printf("No 'bin' directory found, attempting to locate one.\n");
		/* try argv[0] */
		astrncpy(binpath, argv0, PATH_MAX);
		/* make sure asss exists */
		if (stat(binpath, &st) == -1)
			goto no_bin;
		/* get dir name */
		t = strrchr(binpath, '/');
		if (!t)
			goto no_bin;
		*t = 0;
		/* make sure this exists */
		if (stat(binpath, &st) == -1)
			goto no_bin;
		/* link it */
		if (symlink(binpath, "bin") == -1)
		{
			printf("symlink failed. Module loading won't work\n");
		}
		printf("Made link from 'bin' to '%s'.\n", binpath);
		return;
no_bin:
		printf("Can't find suitable bin directory.\n");
	}
	else if (!S_ISDIR(st.st_mode))
		printf("'bin' isn't a directory.\n");
	else /* everything is fine */
		return;
	printf("Module loading won't work.\n");
#endif
}


local void LoadModuleFile(char *fname)
{
	char line[256];
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

#ifndef WIN32
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
#else
local int daemonize(int noclose)
{
	printf("daemonize isn't supported on windows\n");
	return 0;
}
#endif


int main(int argc, char *argv[])
{
	Ilogman *lm;
	Imainloop *ml;

	ProcessArgs(argc,argv);

	CheckBin(argv[0]);

	mm = InitModuleManager();

	printf("asss %s built at %s\n", ASSSVERSION, BUILDDATE);

	if (dodaemonize)
		daemonize(0);

	printf("Loading modules...\n");

	LoadModuleFile("conf/modules.conf");

	lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
	ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

	if (!ml)
		Error(ERROR_MODLOAD, "mainloop module missing");

	if (lm) lm->Log(L_DRIVEL,"<main> Entering main loop");

	ml->RunLoop();

	if (lm) lm->Log(L_DRIVEL,"<main> Exiting main loop");

	mm->ReleaseInterface(lm);
	mm->ReleaseInterface(ml);

	mm->UnloadAllModules();

	return 0;
}


