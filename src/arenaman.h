
#ifndef __ARENAMAN_H
#define __ARENAMAN_H

#include "config.h"

/* ArenaActionFunc(int action, int arena) returns 1 on success and 0 on
 * failure
 */

typedef int (*ArenaActionFunc)(int, int);

#define AA_LOAD 1
#define AA_UNLOAD 2

#define AA_SUCCESS 1
#define AA_FAIL

/* status conditions */
#define ARENA_NONE 0
#define ARENA_LOADING 1
#define ARENA_UNLOADING 2
#define ARENA_RUNNING 3


#define CALLBACK_ARENAACTION "arenaaction"


typedef struct ArenaData
{
	int status;
	char name[20];
	ConfigHandle cfg;
} ArenaData;


typedef struct Iarenaman
{
	ArenaData *data;
} Iarenaman;



#endif

