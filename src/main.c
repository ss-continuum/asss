

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
	static char line[64];
	int ret;
	FILE *f;

	f = fopen(fname,"r");

	if (!f) return;

	while (fgets(line, 64, f))
	{
		RemoveCRLF(line);
		if (line[0] && line[0] != ';' && line[0] != '#' && line[0] != '/')
		{
			ret = mm->LoadModule(line);
			if (ret == MM_FAIL)
			{
				fprintf(stderr,"Error in loading module '%s'", line);
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

	log->Log(LOG_DEBUG,"Entering main loop");

	ml->RunLoop();

	log->Log(LOG_DEBUG,"Exiting main loop");

	mm->UnregInterest(I_LOGMAN, &log);
	mm->UnregInterest(I_MAINLOOP, &ml);

	mm->UnloadAllModules();

	return 0;
}


