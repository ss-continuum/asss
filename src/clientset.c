
#include <stdlib.h>
#include <assert.h>

#include "asss.h"


#include "packets/clientset.h"


#define COUNT(x) (sizeof(x)/sizeof(x[0]))


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

/* the client settings definition */
#include "clientset.def"


int MM_clientset(int action, Imodman *mm_, int arena)
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

		mm->RegCallback(CALLBACK_ARENAACTION, ActionFunc, ALLARENAS);

		mm->RegInterface(I_CLIENTSET, &_myint);

		/* do these at least once */
		{
			struct ClientSettings cs;
			struct ShipSettings ss;

			assert(COUNT(cs.long_set) == COUNT(long_names));
			assert(COUNT(cs.short_set) == COUNT(short_names));
			assert(COUNT(cs.byte_set) == COUNT(byte_names));
			assert(COUNT(cs.prizeweight_set) == COUNT(prizeweight_names));
			assert(COUNT(ss.long_set) == COUNT(ship_long_names));
			assert(COUNT(ss.short_set) == COUNT(ship_short_names));
			assert(COUNT(ss.byte_set) == COUNT(ship_byte_names));
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CLIENTSET, &_myint);
		mm->UnregCallback(CALLBACK_ARENAACTION, ActionFunc, ALLARENAS);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
		return MM_OK;
	}
	return MM_FAIL;
}


void LoadSettings(int arena)
{
	struct ClientSettings *cs = settings + arena;
	ConfigHandle conf;
	int i, j;

	/* get the file */
	conf = arenas[arena].cfg;

	/* clear and set type */
	memset(cs, 0, sizeof(*cs));
	cs->type = S2C_SETTINGS;

	/* do ships */
	for (i = 0; i < 8; i++)
	{
		struct WeaponBits wb;
		struct ShipSettings *ss = cs->ships + i;
		char *shipname = ship_names[i];

		/* basic stuff */
		for (j = 0; j < COUNT(ss->long_set); j++)
			ss->long_set[j] = cfg->GetInt(conf,
					shipname, ship_long_names[j], 0);
		for (j = 0; j < COUNT(ss->short_set); j++)
			ss->short_set[j] = cfg->GetInt(conf,
					shipname, ship_short_names[j], 0);
		for (j = 0; j < COUNT(ss->byte_set); j++)
			ss->byte_set[j] = cfg->GetInt(conf,
					shipname, ship_byte_names[j], 0);

		/* weapons bits */
#define DO(x) \
		wb.x = cfg->GetInt(conf, shipname, #x, 0)
		DO(ShrapnelMax); DO(ShrapnelRate);  DO(AntiWarpStatus);
		DO(CloakStatus); DO(StealthStatus); DO(XRadarStatus);
		DO(InitialGuns); DO(MaxGuns);       DO(InitialBombs);
		DO(MaxBombs);    DO(DoubleBarrel);  DO(EmpBomb);
		DO(SeeMines);    DO(Unused1);
#undef DO
		ss->Weapons = wb;
	}

	/* do settings */
	for (i = 0; i < COUNT(cs->long_set); i++)
		cs->long_set[i] = cfg->GetInt(conf, long_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->short_set); i++)
		cs->short_set[i] = cfg->GetInt(conf, short_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->byte_set); i++)
		cs->byte_set[i] = cfg->GetInt(conf, byte_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->prizeweight_set); i++)
		cs->prizeweight_set[i] = cfg->GetInt(conf,
				prizeweight_names[i], NULL, 0);

	/* the funky ones */
	cs->long_set[1] *= 1000; /* BombDamageLevel */
	cs->long_set[11] *= 1000; /* BulletDamageLevel */
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


