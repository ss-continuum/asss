
/* dist: public */

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


#define I_ARENAMAN "arenaman-3"

typedef struct Iarenaman
{
	INTERFACE_HEAD_DECL

	void (*SendArenaResponse)(int pid);
	void (*LeaveArena)(int pid);

	void (*SendToArena)(int pid, const char *aname, int spawnx, int spawny);
	/* works on cont clients only. set spawnx/y to 0 for default spawn */

	int (*FindArena)(const char *name, int *totalcount, int *playing);
	/* this is a multi-purpose function. given a name, it returns either
	 * an arena id (if some arena by that name is running) or -1 (if
	 * not). if it's running, it also fills in the next two params with
	 * the number of players in the arena and the number of non-spec
	 * players in the arena. */

	void (*LockStatus)(void);
	void (*UnlockStatus)(void);
	/* use these before accessing the big array */

	ArenaData *arenas;
	/* this is a big array of public data */
} Iarenaman;


#define I_ARENAPLACE "arenaplace-1"

typedef struct Iarenaplace
{
	INTERFACE_HEAD_DECL
	int (*Place)(char *name, int namelen, int pid);
	/* this should put an arena name in name, which has namelen space.
	 * if it puts a name there, it should return true, if not (it failed
	 * for some reason), return false. */
} Iarenaplace;


#endif

