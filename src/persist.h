
#ifndef __PERSIST_H
#define __PERSIST_H

/*
 * Ipersist - score manager
 *
 * this manages all player scores (and other persistent data, if there
 * ever is any).
 *
 * usage works like this: other modules register PersistantData
 * descriptors with persist. key is a unique number that will identify
 * the type of data. length is the number of bytes you want to store, up
 * to MAXPERSISTLENGTH. global is either 0, meaning each player gets one
 * copy of the data for the whole server, or 1, meaning the data is
 * maintained per arena. the two functions will be used to get the data
 * for storing, and to set it when retrieving.
 *
 * when a player connects to the server, SyncFromFile will be called
 * (hopefully in another thread) which will read that player's
 * persistant information from the file and call the SetData of all
 * registered PersistantData descriptors. the global flag is 1 when
 * syncing global data is desired, and 0 when arena data is desired.
 * SyncToFile will be called when a player is disconnecting, which will
 * call each data descriptor's GetData function to get the data to write
 * to the file. when switching arenas, the previous arena's data will be
 * synced to the file, and then the new arena's information synced from
 * it.
 *
 * a few things to keep in mind: the PersistantData structure should
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


#define MAXPERSISTLENGTH 1024

#define PERSIST_GLOBAL (-1)


typedef struct PersistantData
{
	int key, length, global;
	void (*GetData)(int pid, void *data);
	void (*SetData)(int pid, void *data);
	void (*ClearData)(int pid);
} PersistantData;


typedef struct Ipersist
{
	INTERFACE_HEAD_DECL
	void (*RegPersistantData)(PersistantData const *pd);
	void (*UnregPersistantData)(PersistantData const *pd);
	void (*SyncToFile)(int pid, int arena, void (*callback)(int pid));
	void (*SyncFromFile)(int pid, int arena, void (*callback)(int pid));
	void (*StabilizeScores)(int seconds);
} Ipersist;


#endif


