


#include <stdlib.h>

#include "asss.h"


/* structs for packet types and data */
struct TileData
{
	unsigned x : 12;
	unsigned y : 12;
	unsigned type : 8;
};



/* prototypes */


/* global data */

/* cached interfaces */
local Ilogman *log;

/* cached data pointers */
local ArenaData **arenas;

/* this module's interface */
local Imapdata _int =
{
};




int MM_mapdata(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_LOGMAN, &log);

		if (!log || !core) return MM_FAIL;

		arenas = core->arenas;

		mm->RegInterface(I_MAPDATA, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(&_int);
		mm->UnregInterest(I_LOGMAN, &log);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "xxx - ";
	}
	return MM_OK;
}



