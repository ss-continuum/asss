

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "asss.h"




local Imodman *mm;



local void ProcessArgs(int argc, char *argv[])
{

}


local void LoadModuleFile(char *fname)
{
	static char line[256];
	int ret;
	FILE *f;

	f = fopen(fname,"r");

	if (!f)
	{
		fprintf(stderr, "Couldn't open '%s'\n", fname);
		exit(2);
	}

	while (fgets(line, 256, f))
	{
		RemoveCRLF(line);
		if (line[0] && line[0] != ';' && line[0] != '#' && line[0] != '/')
		{
			ret = mm->LoadModule(line);
			if (ret == MM_FAIL)
			{
				fprintf(stderr,"Error in loading module '%s'\n", line);
				exit(1);
			}
		}
	}

	fclose(f);
}


int main(int argc, char *argv[])
{
	Ilogman *log;
	Imainloop *ml;

	ProcessArgs(argc,argv);

	mm = InitModuleManager();

	printf("asss 0.1\nLoading modules...\n");

	LoadModuleFile("conf/modules.conf");

	mm->RegInterest(I_LOGMAN, &log);
	mm->RegInterest(I_MAINLOOP, &ml);

	log->Log(L_DRIVEL,"<main> Entering main loop");

	ml->RunLoop();

	log->Log(L_DRIVEL,"<main> Exiting main loop");

	mm->UnregInterest(I_LOGMAN, &log);
	mm->UnregInterest(I_MAINLOOP, &ml);

	mm->UnloadAllModules();

	return 0;
}


