
#include <string.h>

#include "asss.h"


/* prototypes */
local void LockPlayer(int pid);
local void UnlockPlayer(int pid);
local void LockStatus(void);
local void UnlockStatus(void);
local int FindPlayer(const char *name);
local void TargetToSet(const Target *target, int set[MAXPLAYERS+1]);


/* static data */

local pthread_mutex_t playermtx[MAXPLAYERS];
local pthread_mutex_t statusmtx;

/* the big player array! */
local PlayerData players[MAXPLAYERS+EXTRA_PID_COUNT];

/* interface */
local Iplayerdata _myint =
{
	INTERFACE_HEAD_INIT(I_PLAYERDATA, "playerdata")
	players,
	LockPlayer, UnlockPlayer,
	LockStatus, UnlockStatus,
	FindPlayer,
	TargetToSet
};


EXPORT int MM_playerdata(int action, Imodman *mm, int arena)
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

		/* init some basic data */
		for (i = 0; i < MAXPLAYERS; i++)
		{
			players[i].status = S_FREE;
			players[i].arena = -1;
			players[i].attachedto = -1;
		}

		/* register interface */
		mm->RegInterface(&_myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		/* destroy mutexes */
		for (i = 0; i < MAXPLAYERS; i++)
			pthread_mutex_destroy(playermtx + i);
		pthread_mutex_destroy(&statusmtx);
		return MM_OK;
	}
	return MM_FAIL;
}


void LockPlayer(int pid)
{
	if (PID_OK(pid))
		pthread_mutex_lock(playermtx + pid);
}

void UnlockPlayer(int pid)
{
	if (PID_OK(pid))
		pthread_mutex_unlock(playermtx + pid);
}

void LockStatus(void)
{
	pthread_mutex_lock(&statusmtx);
}

void UnlockStatus(void)
{
	pthread_mutex_unlock(&statusmtx);
}


int FindPlayer(const char *name)
{
	int i;
	PlayerData *p;

	pthread_mutex_lock(&statusmtx);
	for (i = 0, p = players; i < MAXPLAYERS; i++, p++)
		if (p->status != S_FREE &&
		    strcasecmp(name, p->name) == 0)
		{
			pthread_mutex_unlock(&statusmtx);
			return i;
		}
	pthread_mutex_unlock(&statusmtx);
	return -1;
}


local inline int matches(const Target *t, int pid)
{
	switch (t->type)
	{
		case T_NONE:
			return 0;

		case T_PID:
			return pid == t->u.pid;

		case T_ARENA:
			return players[pid].arena == t->u.arena;

		case T_FREQ:
			return players[pid].arena == t->u.freq.arena &&
			       players[pid].freq == t->u.freq.freq;

		case T_ZONE:
			return 1;

		default:
			return 0;
	}
}

void TargetToSet(const Target *target, int set_[MAXPLAYERS+1])
{
	int *set = set_, i;

	if (target->type == T_SET)
	{
		int *src = target->u.set;
		while (*src != -1)
			*set++ = *src++;
		*set = -1;
	}
	else
	{
		pthread_mutex_lock(&statusmtx);
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status == S_PLAYING &&
			    matches(target, i))
				*set++ = i;
		*set = -1;
		pthread_mutex_unlock(&statusmtx);
	}
}


