
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
 * identify the type of data. type is either PD_INT, for an integer data
 * type, or PD_STRING, for a string. (ints are much more efficient, so
 * use them if possible) global is either 0, meaning each player gets
 * one copy of the data for the whole server, or 1, meaning the data is
 * maintained per arena. the exact functions used to access the data
 * will vary by type. basically, GetX must return a value to be stored
 * in the file, and SetX must do something with a value read from the
 * file.
 *
 * when a player connects to the server, SyncFromFile will be called,
 * which will read that player's persistant information from the file
 * and call the SetX of all registered PersistantData descriptors.  the
 * global flag is 1 when syncing global data is desired, and 0 when
 * arena data is desired. SyncToFile will be called when a player is
 * disconnecting, which will call each data descriptor's GetX function
 * to get the data to write to the file. when switching arenas, the
 * previous arena's data will be synced to the file, and then the new
 * arena's information synced from it.
 *
 * a few things to keep in mind: the PersistantData structure should
 * never change while the program is running. furthermore, the key,
 * type, and global fields should never change at all, even across runs,
 * or any previously created files will become useless. the GetX and
 * SetX functions can be called from any thread.
 *
 */

#define PD_INT 1
#define PD_STRING 2

typedef struct PersistantData
{
	int key, type, global;
	/* type == PD_INT */
	int (*GetInt)(int pid);
	void  (*SetInt)(int pid, int data);
	/* type == PD_STRING */
	char * (*GetString)(int pid);
	void (*ReleaseString)(char *data);
	void (*SetString)(int pid, char *data);
} PersistantData;


typedef struct Iscoreman
{
	void (*RegPersistantData)(PersistantData *pd);
	void (*UnregPersistantData)(PersistantData *pd);
	int (*SyncToFile)(int pid, int global);
	int (*SyncFromFile)(int pid, int global);
} Iscoreman;

#endif


