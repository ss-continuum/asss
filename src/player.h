
#ifndef __PLAYER_H
#define __PLAYER_H

#include "packets/pdata.h"

typedef struct Iplayerdata
{
	PlayerData *players;
	void (*LockPlayer)(int pid);
	void (*UnlockPlayer)(int pid);
	void (*LockStatus)();
	void (*UnlockStatus)();
} Iplayerdata;

#endif

