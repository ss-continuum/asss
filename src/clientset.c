
#include <stdlib.h>
#include <assert.h>

#include "asss.h"


#include "packets/clientset.h"


/* prototypes */
local void LoadSettings(int arena);
local void ActionFunc(int arena, int action);
local void SendClientSettings(int pid);
local void Reconfigure(int arena);

/* global data */

/* this array is pretty big. about 27k */
local struct ClientSettings settings[MAXARENA];

/* cached interfaces */
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *log;
local Imodman *mm;
local Iarenaman *aman;

/* cached data pointers */
local ArenaData *arenas;

/* interfaces */
local Iclientset _myint = { SendClientSettings, Reconfigure };


int MM_clientset(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_ARENAMAN, &aman);

		if (!net || !cfg || !log || !aman) return MM_FAIL;

		arenas = aman->data;

		mm->RegCallback(CALLBACK_ARENAACTION, ActionFunc);

		mm->RegInterface(I_CLIENTSET, &_myint);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CLIENTSET, &_myint);
		mm->UnregCallback(CALLBACK_ARENAACTION, ActionFunc);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "clientset - manages client-side settings";
	}
	return MM_OK;
}


void LoadSettings(int arena)
{
	struct ClientSettings *cs = settings + arena;

	cs->type = S2C_SETTINGS;
}


void ActionFunc(int arena, int action)
{
	if (action == AA_CREATE)
	{
		LoadSettings(arena);
	}
	else if (action == AA_DESTROY)
	{
		/* mark settings as destroyed (for asserting later) */
		settings[arena].type = 0;
	}
}


void SendClientSettings(int pid)
{
	byte *data = (byte*)(settings + pd->players[pid].arena);
	/* this has the side-effect of asserting little-endianness */
	assert(data[0] == S2C_SETTINGS);
	net->SendToOne(pid, data, sizeof(struct ClientSettings), NET_RELIABLE);
}


void Reconfigure(int arena)
{
	byte *data = (byte*)(settings + arena);

	LoadSettings(arena);

	net->SendToArena(arena, -1, data, sizeof(struct ClientSettings), NET_RELIABLE);
}


