
#ifndef __SCOREMAN_H
#define __SCOREMAN_H

/*
 * Iscoreman - score manager
 *
 * this manages all player scores (and other persistent data, if there
 * ever is any).
 *
 * usage works like this: other modules register PersistantData
 * descriptors with the scoreman. key is a unique number that will
 * identify the type of data. length is the number of bytes you want to
 * store, up to MAXPERSISTLENGTH. global is either 0, meaning each
 * player gets one copy of the data for the whole server, or 1, meaning
 * the data is maintained per arena. the two functions will be used to
 * get the data for storing, and to set it when retrieving.
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
 * runs, or any previously created files will become useless. the
 * GetData and SetData functions can be called from any thread, so the
 * must be sure to do whatever locking is appropriate.
 *
 */


#define MAXPERSISTLENGTH 64


typedef struct PersistantData
{
	int key, length, global;
	void (*GetData)(int pid, void *data);
	void (*SetData)(int pid, void *data);
} PersistantData;


typedef struct Iscoreman
{
	void (*RegPersistantData)(PersistantData *pd);
	void (*UnregPersistantData)(PersistantData *pd);
	int (*SyncToFile)(int pid, int global);
	int (*SyncFromFileAsync)(int pid, int global, void (*callback)(int pid));
	int (*SyncFromFile)(int pid, int global);
} Iscoreman;


#endif


