
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "asss.h"
#include "clientset.h"


#include "packets/clientset.h"


#define COUNT(x) (sizeof(x)/sizeof(x[0]))


/* prototypes */
local void ActionFunc(Arena *arena, int action);
local void SendClientSettings(int pid);
local void Reconfigure(Arena *arena);
local u32 GetChecksum(Arena *arena, u32 key);

/* global data */

local int csetkey;
local pthread_mutex_t setmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&setmtx)
#define UNLOCK() pthread_mutex_unlock(&setmtx)

/* cached interfaces */
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;

/* interfaces */
local Iclientset _myint =
{
	INTERFACE_HEAD_INIT(I_CLIENTSET, "clientset")
	SendClientSettings, Reconfigure, GetChecksum
};

/* the client settings definition */
#include "clientset.def"


EXPORT int MM_clientset(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);

		if (!net || !cfg || !lm || !aman) return MM_FAIL;

		csetkey = aman->AllocateArenaData(sizeof(struct ClientSettings));
		if (csetkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, ActionFunc, ALLARENAS);

		mm->RegInterface(&_myint, ALLARENAS);

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
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_ARENAACTION, ActionFunc, ALLARENAS);
		aman->FreeArenaData(csetkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		return MM_OK;
	}
	return MM_FAIL;
}


/* call with lock held! */
local void LoadSettings(Arena *arena)
{
	struct ClientSettings *cs = P_ARENA_DATA(arena, csetkey);
	ConfigHandle conf;
	int i, j;

	/* get the file */
	conf = arena->cfg;

	/* clear and set type */
	memset(cs, 0, sizeof(*cs));
	cs->bit_set.type = S2C_SETTINGS;

	cs->bit_set.ExactDamage = cfg->GetInt(conf, "Bullet", "ExactDamage", 0);
	cs->bit_set.HideFlags = cfg->GetInt(conf, "Spectator", "HideFlags", 0);
	cs->bit_set.NoXRadar = cfg->GetInt(conf, "Spectator", "NoXRadar", 0);

	/* do ships */
	for (i = 0; i < 8; i++)
	{
		struct WeaponBits wb;
		struct MiscBitfield misc;
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
		DO(ShrapnelMax);  DO(ShrapnelRate);  DO(AntiWarpStatus);
		DO(CloakStatus);  DO(StealthStatus); DO(XRadarStatus);
		DO(InitialGuns);  DO(MaxGuns);
		DO(InitialBombs); DO(MaxBombs);
		DO(DoubleBarrel); DO(EmpBomb); DO(SeeMines);
		DO(Unused1);
#undef DO
		ss->Weapons = wb;

		/* now do the strange bitfield */
		memset(&misc, 0, sizeof(misc));
		misc.SeeBombLevel = cfg->GetInt(conf, shipname, "SeeBombLevel", 0);
		misc.DisableFastShooting = cfg->GetInt(conf, shipname,
				"DisableFastShooting", 0);
		misc.Radius = cfg->GetInt(conf, shipname, "Radius", 0);
		ss->short_set[10] = *(unsigned short*)&misc;
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
	cs->long_set[0] *= 1000; /* BombDamageLevel */
	cs->long_set[1] *= 1000; /* BombDamageLevel */
	cs->long_set[10] *= 1000; /* BurstDamageLevel */
	cs->long_set[11] *= 1000; /* BulletDamageLevel */
	cs->long_set[16] *= 1000; /* InactiveShrapDamage */
}


void ActionFunc(Arena *arena, int action)
{
	LOCK();
	if (action == AA_CREATE)
	{
		LoadSettings(arena);
	}
	else if (action == AA_CONFCHANGED)
	{
		byte *data = P_ARENA_DATA(arena, csetkey);
		LoadSettings(arena);
		net->SendToArena(arena, -1, data, sizeof(struct ClientSettings), NET_RELIABLE);
		lm->LogA(L_INFO, "clientset", arena, "Sending modified settings");
	}
	else if (action == AA_DESTROY)
	{
		/* mark settings as destroyed (for asserting later) */
		struct ClientSettings *cs = P_ARENA_DATA(arena, csetkey);
		cs->bit_set.type = 0;
	}
	UNLOCK();
}


void SendClientSettings(int pid)
{
	byte *data = P_ARENA_DATA(pd->players[pid].arena, csetkey);
	LOCK();
	if (pd->players[pid].arena && data[0] == S2C_SETTINGS)
		net->SendToOne(pid, data, sizeof(struct ClientSettings), NET_RELIABLE);
	UNLOCK();
}


void Reconfigure(Arena *arena)
{
	byte *data = P_ARENA_DATA(arena, csetkey);
	LOCK();
	LoadSettings(arena);
	net->SendToArena(arena, -1, data, sizeof(struct ClientSettings), NET_RELIABLE);
	UNLOCK();
}


u32 GetChecksum(Arena *arena, u32 key)
{
	u32 csum = 0, i;
	u32 *data = P_ARENA_DATA(arena, csetkey);
	LOCK();
	for (i = 0; i < 357; i++, data++)
		csum += (*data ^ key);
	UNLOCK();
	return csum;
}


