
#include "asss.h"

#define KEY_JACKPOT 12

local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ipersist *persist;

local pthread_mutex_t mtx;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

local struct
{
	int jp;
	int percent;
} data[MAXARENA];


local void ResetJP(int arena)
{
	LOCK();
	data[arena].jp = 0;
	UNLOCK();
}

local void AddJP(int arena, int pts)
{
	LOCK();
	data[arena].jp += pts;
	UNLOCK();
}

local int GetJP(int arena)
{
	int jp;
	LOCK();
	jp = data[arena].jp;
	UNLOCK();
	return jp;
}


local int get_data(int arena, void *d, int len)
{
	int jp = GetJP(arena);
	if (jp)
	{
		*(int*)d = jp;
		return sizeof(int);
	}
	else
		return 0;
}

local void set_data(int arena, void *d, int len)
{
	if (len == sizeof(int))
	{
		LOCK();
		data[arena].jp = *(int*)d;
		UNLOCK();
	}
	else
		ResetJP(arena);
}

local void clear_data(int arena)
{
	ResetJP(arena);
}

local PersistentData jpdata =
{
	KEY_JACKPOT, PERSIST_ALLARENAS, INTERVAL_GAME,
	get_data, set_data, clear_data
};


local void kill(int arena, int killer, int killed, int bounty, int flags)
{
	LOCK();
	if (data[arena].percent)
		data[arena].jp += bounty * data[arena].percent / 1000;
	UNLOCK();
}

local void aaction(int arena, int action)
{
	LOCK();
	/* cfghelp: Kill:JackpotBountyPercent, arena, int, def: 0
	 * The percent of a player's bounty added to the jackpot on each
	 * kill. Units: 0.1%. */
	if (action == AA_CREATE || action == AA_CONFCHANGED)
		data[arena].percent = cfg->GetInt(aman->arenas[arena].cfg, "Kill", "JackpotBountyPercent", 0);
	UNLOCK();
}


local Ijackpot jpint =
{
	INTERFACE_HEAD_INIT(I_JACKPOT, "jackpot")
	ResetJP, AddJP, GetJP
};


EXPORT int MM_jackpot(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (!aman || !cfg || !persist) return MM_FAIL;

		pthread_mutex_init(&mtx, NULL);

		mm->RegCallback(CB_KILL, kill, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		persist->RegArenaPD(&jpdata);

		mm->RegInterface(&jpint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&jpint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_KILL, kill, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		persist->UnregArenaPD(&jpdata);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(persist);
		pthread_mutex_destroy(&mtx);
		return MM_OK;
	}
	return MM_FAIL;
}

