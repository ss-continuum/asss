


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
local ArenaData *arenas;

/* this module's interface */
local Imapdata _int =
{
};




int MM_mapdata(int action, Imodman *mm, int arenas)
{
	if (action == MM_LOAD)
	{
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		return MM_OK;
	}
	return MM_FAIL;
}



