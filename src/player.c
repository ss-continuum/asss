
#include "asss.h"


/* prototypes */
local void LockPlayer(int pid);
local void UnlockPlayer(int pid);
local void LockStatus();
local void UnlockStatus();


/* static data */

local pthread_mutex_t playermtx[MAXPLAYERS];
local pthread_mutex_t statusmtx;

/* the big player array! */
local PlayerData players[MAXPLAYERS+EXTRA_PID_COUNT];

/* interface */
local Iplayerdata _myint =
	{ players, LockPlayer, UnlockPlayer, LockStatus, UnlockStatus };


int MM_playerdata(int action, Imodman *mm)
{
	int i;
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		/* init mutexes */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		for (i = 0; i < MAXPLAYERS; i++)
			pthread_mutex_init(playermtx + i, &attr);
		pthread_mutex_init(&statusmtx, NULL);
		pthread_mutexattr_destroy(&attr);

		/* register interface */
		mm->RegInterface(I_PLAYERDATA, &_myint);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_PLAYERDATA, &_myint);
		/* destroy mutexes */
		for (i = 0; i < MAXPLAYERS; i++)
			pthread_mutex_destroy(playermtx + i);
		pthread_mutex_destroy(&statusmtx);
	}
	return MM_OK;
}


void LockPlayer(int pid)
{
	if (pid >= 0 && pid < MAXPLAYERS)
		pthread_mutex_lock(playermtx + pid);
}

void UnlockPlayer(int pid)
{
	if (pid >= 0 && pid < MAXPLAYERS)
		pthread_mutex_unlock(playermtx + pid);
}

void LockStatus()
{
	pthread_mutex_lock(&statusmtx);
}

void UnlockStatus()
{
	pthread_mutex_unlock(&statusmtx);
}

