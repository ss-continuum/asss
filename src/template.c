

#include <stdlib.h>

#include "asss.h"


/* structs for packet types and data */


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




int MM_xxx(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		Icore *core;

		mm = mm_;
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



