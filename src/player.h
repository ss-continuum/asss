
#ifndef __PLAYER_H
#define __PLAYER_H

#include "packets/pdata.h"

typedef struct Iplayerdata
{
	PlayerData *players;
	void (*LockPlayer)(int pid);
	void (*UnlockPlayer)(int pid);
	void (*LockStatus)(void);
	void (*UnlockStatus)(void);
	int (*FindPlayer)(char *name);
	/* this is a useful function that doesn't belong here. but it is
	 * anyway. */
} Iplayerdata;

#endif

