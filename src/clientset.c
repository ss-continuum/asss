
#include <stdlib.h>

#include "asss.h"


#include "packets/clientset.h"


/* prototypes */


/* global data */

/* cached interfaces */
local Inet *net;
local Iconfig *cfg;
local Ilogman *log;
local Icmdman *cmd;
local Imodman *mm;

/* cached data pointers */
local PlayerData *players;
local ArenaData **arenas;

/* this module's interface */
local Ixxx _int =
{
};




int MM_clientset(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CMDMAN, &cmd);

		if (!net || !cfg || !log || !core) return MM_FAIL;

		players = mm->players;
		arenas = core->arenas;

		mm->RegInterface(I_XXX, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(&_int);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CMDMAN, &cmd);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "xxx - ";
	}
	return MM_OK;
}



