
#ifndef __PLAYER_H
#define __PLAYER_H

#include "packets/pdata.h"


#define PID_OK(pid) \
	((pid) >= 0 && (pid) < MAXPLAYERS)

#define PID_BAD(pid) \
	((pid) < 0 || (pid) >= MAXPLAYERS)


#define I_PLAYERDATA "playerdata-1"

typedef struct Iplayerdata
{
	INTERFACE_HEAD_DECL

	PlayerData *players;
	/* arpc: null */
	void (*LockPlayer)(int pid);
	/* arpc: void(int) noop */
	void (*UnlockPlayer)(int pid);
	/* arpc: void(int) noop */
	void (*LockStatus)(void);
	/* arpc: void(void) noop */
	void (*UnlockStatus)(void);
	/* arpc: void(void) noop */
	int (*FindPlayer)(const char *name);
	/* arpc: int(string) */
} Iplayerdata;

#endif

