
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




int MM_clientset(int action, Imodman *mm2)
{
	if (action == MM_LOAD)
	{
		Icore *core;

		mm = mm2;
		net = mm->GetInterface(I_NET);
		cfg = mm->GetInterface(I_CONFIG);
		log = mm->GetInterface(I_LOGMAN);
		core = mm->GetInterface(I_CORE);
		cmd = mm->GetInterface(I_CMDMAN);

		if (!net || !cfg || !log || !core) return MM_FAIL;

		players = mm->players;
		arenas = core->arenas;

		mm->RegisterInterface(I_XXX, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_int);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "xxx - ";
	}
	return MM_OK;
}



