
#ifndef __ARENAMAN_H
#define __ARENAMAN_H

#include "config.h"

/* ArenaAction funcs are called when arenas are created or destroyed */

#define CALLBACK_ARENAACTION "arenaaction"

#define AA_CREATE   1
#define AA_DESTROY  2

typedef void (*ArenaActionFunc)(int arena, int action);

/* status conditions */

#define ARENA_NONE                  0
/* free arena ids have this status */

#define ARENA_DO_LOAD_CONFIG        1
/* someone wants to enter the arena. first, the config file must be
 * loaded */

#define ARENA_DO_CREATE_CALLBACKS   2
/* and the arena creation callbacks called */

#define ARENA_RUNNING               3
/* now the arena is fully created. core can now send the arena responses
 * to players waiting to enter this arena */

#define ARENA_DO_DESTROY_CALLBACKS  4
/* the arena is being reaped, first call destroy callbacks */

#define ARENA_DO_UNLOAD_CONFIG      5
/* then unload the config file. status returns to free after this */



typedef struct ArenaData
{
	int status;
	char name[20];
	ConfigHandle cfg;
} ArenaData;


typedef struct Iarenaman
{
	void (*SendArenaResponse)(int pid);
	void (*LockStatus)(void);
	void (*UnlockStatus)(void);
	ArenaData *arenas;
} Iarenaman;



#endif

