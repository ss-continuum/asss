
#ifndef __PLAYER_H
#define __PLAYER_H

#include "packets/pdata.h"

#define PID_OK(pid) \
	((pid) >= 0 && (pid) < MAXPLAYERS)

#define PID_BAD(pid) \
	((pid) < 0 || (pid) >= MAXPLAYERS)

typedef struct Iplayerdata
{
	PlayerData *players;
	void (*LockPlayer)(int pid);
	void (*UnlockPlayer)(int pid);
	void (*LockStatus)(void);
	void (*UnlockStatus)(void);
	int (*FindPlayer)(char *name);
} Iplayerdata;

#endif

