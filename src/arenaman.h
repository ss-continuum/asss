
#ifndef __ARENAMAN_H
#define __ARENAMAN_H

#include "config.h"

#define ARENA_OK(arena) \
	((arena) >= 0 && (arena) < MAXARENA)

#define ARENA_BAD(arena) \
	((arena) < 0 || (arena) >= MAXARENA)

/* ArenaAction funcs are called when arenas are created or destroyed */

#define CB_ARENAACTION "arenaaction"

enum
{
	/* called when arena is created */
	AA_CREATE,
	/* called when config file changes */
	AA_CONFCHANGED,
	/* called when the arena is destroyed */
	AA_DESTROY
};

typedef void (*ArenaActionFunc)(int arena, int action);

/* status conditions */

enum
{
	ARENA_NONE,
/* free arena ids have this status */

	ARENA_DO_INIT,
/* someone wants to enter the arena. first, the config file must be
 * loaded, callbacks called, and the persistant data loaded  */

	ARENA_WAIT_SYNC1,
/* waiting on the database */

	ARENA_RUNNING,
/* now the arena is fully created. core can now send the arena responses
 * to players waiting to enter this arena */

	ARENA_DO_WRITE_DATA,
/* the arena is being reaped, first put info in database */

	ARENA_WAIT_SYNC2,
/* waiting on the database to finish before we can unregister modules */

	ARENA_DO_DEINIT
/* then unload the config file. status returns to free after this */
};



typedef struct ArenaData
{
	int status, ispublic;
	char name[20], basename[20];
	ConfigHandle cfg;
} ArenaData;


#define I_ARENAMAN "arenaman-1"

typedef struct Iarenaman
{
	INTERFACE_HEAD_DECL

	void (*SendArenaResponse)(int pid);
	/* arpc: void(int) */
	void (*LeaveArena)(int pid);
	/* arpc: void(int) */
	void (*LockStatus)(void);
	/* arpc: void(void) noop */
	void (*UnlockStatus)(void);
	/* arpc: void(void) noop */
	ArenaData *arenas;
	/* arpc: null */
} Iarenaman;


#define I_ARENAPLACE "arenaplace-1"

typedef struct Iarenaplace
{
	INTERFACE_HEAD_DECL
	int (*Place)(char *name, int namelen, int pid);
} Iarenaplace;


#endif

