
/* dist: public */

#ifndef __ARENAMAN_H
#define __ARENAMAN_H

#include "config.h"


struct Arena
{
	int status, ispublic;
	char name[20], basename[20];
	ConfigHandle cfg;
	byte arenaextradata[0];
};


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

typedef void (*ArenaActionFunc)(Arena *a, int action);


/* status conditions */
enum
{
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


#define I_ARENAMAN "arenaman-4"

typedef struct Iarenaman
{
	INTERFACE_HEAD_DECL

	void (*SendArenaResponse)(Player *p);
	void (*LeaveArena)(Player *p);

	void (*SendToArena)(Player *p, const char *aname, int spawnx, int spawny);
	/* works on cont clients only. set spawnx/y to 0 for default spawn. */

	Arena * (*FindArena)(const char *name, int *totalcount, int *playing);
	/* this is a multi-purpose function. given a name, it returns either
	 * an arena (if some arena by that name is running) or NULL (if
	 * not). if it's running, it also fills in the next two params with
	 * the number of players in the arena and the number of non-spec
	 * players in the arena. */

	int (*AllocateArenaData)(size_t bytes);
	/* returns -1 on failure */
	void (*FreeArenaData)(int key);

	void (*Lock)(void);
	void (*Unlock)(void);
	/* these must always be used to iterate over all the arenas
	 * (with the FOR_EACH_ARENA macro). */

	LinkedList arenalist;
} Iarenaman;


/* use this to access per-arena data */
#define P_ARENA_DATA(a, mykey) ((void*)((a)->arenaextradata+mykey))

/* these assume you have a Link * named 'link' and that 'aman' points to
 * the arena manager interface. don't forget to use aman->Lock() first. */
#define FOR_EACH_ARENA(a) \
	for ( \
			link = LLGetHead(&aman->arenalist); \
			link && ((a = link->data, link = link->next) || 1); )

#define FOR_EACH_ARENA_P(a, d, key) \
	for ( \
			link = LLGetHead(&aman->arenalist); \
			link && ((a = link->data, \
			          d = P_ARENA_DATA(a, key), \
			          link = link->next) || 1); )


#define I_ARENAPLACE "arenaplace-2"

typedef struct Iarenaplace
{
	INTERFACE_HEAD_DECL
	int (*Place)(char *name, int namelen, int *spawnx, int *spawny, Player *p);
	/* this should put an arena name in name, which has namelen space.
	 * if it puts a name there, it should return true, if not (it failed
	 * for some reason), return false. */
} Iarenaplace;


#endif

