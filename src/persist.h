
/* dist: public */

#ifndef __PERSIST_H
#define __PERSIST_H

/*
 * Ipersist - score manager
 *
 * this manages all player scores (and other persistent data, if there
 * ever is any).
 *
 * usage works like this: other modules register PersistentData
 * descriptors with persist. key is a unique number that will identify
 * the type of data. length is the number of bytes you want to store, up
 * to MAXPERSISTLENGTH. global is either 0, meaning each player gets one
 * copy of the data for the whole server, or 1, meaning the data is
 * maintained per arena. the two functions will be used to get the data
 * for storing, and to set it when retrieving.
 *
 * when a player connects to the server, SyncFromFile will be called
 * (hopefully in another thread) which will read that player's
 * persistent information from the file and call the SetData of all
 * registered PersistentData descriptors. the global flag is 1 when
 * syncing global data is desired, and 0 when arena data is desired.
 * SyncToFile will be called when a player is disconnecting, which will
 * call each data descriptor's GetData function to get the data to write
 * to the file. when switching arenas, the previous arena's data will be
 * synced to the file, and then the new arena's information synced from
 * it.
 *
 * a few things to keep in mind: the PersistentData structure should
 * never change while the program is running. furthermore, the key,
 * length, and global fields should never change at all, even across
 * runs, or any previously created files will become useless. the player
 * will be locked before any of the Get/Set/ClearData functions are
 * called.
 *
 * StabilizeScores can be called with an integer argument to encure the
 * score files will be in a consistent state for that many seconds. This
 * can be used to perform backups while the server is running.
 *
 */


#include "statcodes.h"


#define MAXPERSISTLENGTH 1024


#define PERSIST_ALLARENAS ((Arena*)(-1))
/* using this for scope means per-player data in every arena */

#define PERSIST_GLOBAL ((Arena*)(-2))
/* using this for scope means per-player data shared among all arenas */


typedef struct PlayerPersistentData
{
	int key, interval;
	Arena *scope;
	int (*GetData)(int pid, void *data, int len);
	void (*SetData)(int pid, void *data, int len);
	void (*ClearData)(int pid);
} PlayerPersistentData;

typedef struct ArenaPersistentData
{
	int key, interval;
	Arena *scope;
	int (*GetData)(Arena *a, void *data, int len);
	void (*SetData)(Arena *a, void *data, int len);
	void (*ClearData)(Arena *a);
} ArenaPersistentData;

/* for per-player data, scope can be a single arena, or one of the
 * constants above. for per-arena data, scope can be a single arena, or
 * PERSIST_ALLARENAS.
 *
 * for per-player data, any data in the forever and reset intervals will
 * be shared among arenas with the same arenagroup. data in game
 * intervals is never shared among arenas. per-arena data is also never
 * shared among arenas.
 */


#define I_PERSIST "persist-3"

typedef struct Ipersist
{
	INTERFACE_HEAD_DECL

	void (*RegPlayerPD)(const PlayerPersistentData *pd);
	void (*UnregPlayerPD)(const PlayerPersistentData *pd);

	void (*RegArenaPD)(const ArenaPersistentData *pd);
	void (*UnregArenaPD)(const ArenaPersistentData *pd);

	void (*PutPlayer)(int pid, Arena *a, void (*callback)(int pid));
	void (*GetPlayer)(int pid, Arena *a, void (*callback)(int pid));

	void (*PutArena)(Arena *a, void (*callback)(Arena *a));
	void (*GetArena)(Arena *a, void (*callback)(Arena *a));

	void (*EndInterval)(Arena *a, int interval);

	void (*StabilizeScores)(int seconds, int query, void (*callback)(int dummy));
} Ipersist;


#endif

