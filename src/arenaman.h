
/* dist: public */

#ifndef __ARENAMAN_H
#define __ARENAMAN_H

#include "config.h"
#include "db_layout.h"


struct Arena
{
	int status;
	char name[20], basename[20];
	ConfigHandle cfg;
	/* this setting is so commonly used, it deserves to be here. */
	int specfreq;
	/* some summary data maintained by the core modules */
	int playing, total;
	byte arenaextradata[0];
};


/* ArenaAction funcs are called when arenas are created or destroyed */

enum
{
	/* called when arena is created */
	AA_CREATE,
	/* called when config file changes */
	AA_CONFCHANGED,
	/* called when the arena is destroyed */
	AA_DESTROY
};

#define CB_ARENAACTION "arenaaction"
typedef void (*ArenaActionFunc)(Arena *a, int action);
/* the python module handles this one internally, so no pycb directive
 * here. */

/* status conditions */
enum
{
	ARENA_DO_INIT,
	/* someone wants to enter the arena. first, the config file must be
	 * loaded, callbacks called, and the persistant data loaded. */

	ARENA_WAIT_SYNC1,
	/* waiting on the database */

	ARENA_RUNNING,
	/* now the arena is fully created. core can now send the arena
	 * responses to players waiting to enter this arena. */

	ARENA_CLOSING,
	/* the arena is running for a little while, but isn't accepting new
	 * players. */

	ARENA_DO_WRITE_DATA,
	/* the arena is being reaped, first put info in database */

	ARENA_WAIT_SYNC2,
	/* waiting on the database to finish before we can unregister
	 * modules */

	ARENA_DO_DEINIT
	/* then unload the config file. status returns to free after this. */
};


#define I_ARENAMAN "arenaman-6"

typedef struct Iarenaman
{
	INTERFACE_HEAD_DECL

	/* pyint: use */

	void (*SendArenaResponse)(Player *p);
	void (*LeaveArena)(Player *p);

	int (*RecycleArena)(Arena *a);
	/* pyint: arena -> int */

	void (*SendToArena)(Player *p, const char *aname, int spawnx, int spawny);
	/* works on cont clients only. set spawnx/y to 0 for default spawn. */
	/* pyint: player, string, int, int -> void */

	Arena * (*FindArena)(const char *name, int *totalcount, int *playing);
	/* this is a multi-purpose function. given a name, it returns either
	 * an arena (if some arena by that name is running) or NULL (if
	 * not). if it's running, it also fills in the next two params with
	 * the number of players in the arena and the number of non-spec
	 * players in the arena. */
	/* pyint: string, int out, int out -> arena */

	void (*GetPopulationSummary)(int *total, int *playing);
	/* this fills in the data in gps _and_ updates the playing and total
	 * fields of each arena. you should be holding the arena lock when
	 * calling this. */
	/* pyint: int out, int out -> void */

	int (*AllocateArenaData)(size_t bytes);
	/* returns -1 on failure */
	void (*FreeArenaData)(int key);

	void (*Lock)(void);
	/* pyint: void -> void */
	void (*Unlock)(void);
	/* pyint: void -> void */
	/* these must always be used to iterate over all the arenas
	 * (with the FOR_EACH_ARENA macro). */

	LinkedList arenalist;
} Iarenaman;


/* this will tell you if an arena is considered a "public" arena */
#define ARENA_IS_PUBLIC(a) (strcmp((a)->basename, AG_PUBLIC) == 0)

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
	/* pyint: use, impl */

	int (*Place)(char *name, int namelen, int *spawnx, int *spawny, Player *p);
	/* this should put an arena name in name, which has namelen space.
	 * if it puts a name there, it should return true, if not (it failed
	 * for some reason), return false. */
	/* pyint: string out, int buflen, int out, int out, player -> int */
} Iarenaplace;


#endif

