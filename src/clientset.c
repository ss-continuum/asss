
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "asss.h"
#include "clientset.h"


#include "packets/clientset.h"


#define COUNT(x) (sizeof(x)/sizeof(x[0]))

#define SIZE (sizeof(struct ClientSettings))

typedef struct adata
{
	struct ClientSettings cs;
	byte overrides[SIZE];
	byte overridemask[SIZE];
	/* prizeweight partial sums. 0-27 are used for now, representing
	 * prizes 1 to 28. */
	unsigned short pwps[32];
} adata;


/* prototypes */
local void ActionFunc(Arena *arena, int action);
local void SendClientSettings(Player *p);
local void Reconfigure(Arena *arena);
local u32 GetChecksum(Arena *arena, u32 key);
local int GetRandomPrize(Arena *arena);
local void Override(Arena *arena, override_key_t key, i32 val);
local void Unoverride(Arena *arena, override_key_t key);
local override_key_t GetOverrideKey(const char *section, const char *key);

/* global data */

local int adkey;
local pthread_mutex_t setmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&setmtx)
#define UNLOCK() pthread_mutex_unlock(&setmtx)

/* cached interfaces */
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Iprng *prng;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;

/* interfaces */
local Iclientset _myint =
{
	INTERFACE_HEAD_INIT(I_CLIENTSET, "clientset")
	SendClientSettings, Reconfigure, GetChecksum, GetRandomPrize,
	Override, Unoverride, GetOverrideKey
};

/* the client settings definition */
#include "clientset.def"


EXPORT int MM_clientset(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if (!net || !cfg || !lm || !aman || !prng) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, ActionFunc, ALLARENAS);

		mm->RegInterface(&_myint, ALLARENAS);

		/* do these at least once */
#define cs (*((struct ClientSettings*)0))
#define ss (*((struct ShipSettings*)0))
		assert(COUNT(cs.long_set) == COUNT(long_names));
		assert(COUNT(cs.short_set) == COUNT(short_names));
		assert(COUNT(cs.byte_set) == COUNT(byte_names));
		assert(COUNT(cs.prizeweight_set) == COUNT(prizeweight_names));
		assert(COUNT(ss.long_set) == COUNT(ship_long_names));
		assert(COUNT(ss.short_set) == COUNT(ship_short_names));
		assert(COUNT(ss.byte_set) == COUNT(ship_byte_names));
#undef cs
#undef ss

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_ARENAACTION, ActionFunc, ALLARENAS);
		aman->FreeArenaData(adkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		return MM_OK;
	}
	return MM_FAIL;
}


local void load_settings(adata *ad, ConfigHandle conf)
{
	struct ClientSettings *cs = &ad->cs;
	int i, j;
	unsigned short total = 0;

	/* clear and set type */
	memset(cs, 0, sizeof(*cs));

	cs->bit_set.type = S2C_SETTINGS;
	cs->bit_set.ExactDamage = cfg->GetInt(conf, "Bullet", "ExactDamage", 0);
	cs->bit_set.HideFlags = cfg->GetInt(conf, "Spectator", "HideFlags", 0);
	cs->bit_set.NoXRadar = cfg->GetInt(conf, "Spectator", "NoXRadar", 0);
	cs->bit_set.SlowFrameRate = cfg->GetInt(conf, "Misc", "SlowFrameCheck", 0);
	cs->bit_set.DisableScreenshot = cfg->GetInt(conf, "Misc", "DisableScreenshot", 0);
	cs->bit_set.MaxTimerDrift = cfg->GetInt(conf, "Misc", "MaxTimerDrift", 0);
	cs->bit_set.DisableBallThroughWalls = cfg->GetInt(conf, "Misc", "DisableBallThroughWalls", 0);
	cs->bit_set.DisableBallKilling = cfg->GetInt(conf, "Misc", "DisableBallKilling", 0);

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

	/* spawn locations */
	for (i = 0; i < 4; i++)
	{
		char xname[] = "Team#-X";
		char yname[] = "Team#-Y";
		char rname[] = "Team#-Radius";
		xname[4] = yname[4] = rname[4] = '0' + i;
		cs->spawn_pos[i].x = cfg->GetInt(conf, "Spawn", xname, 0);
		cs->spawn_pos[i].y = cfg->GetInt(conf, "Spawn", yname, 0);
		cs->spawn_pos[i].r = cfg->GetInt(conf, "Spawn", rname, 0);
	}

	/* do rest of settings */
	for (i = 0; i < COUNT(cs->long_set); i++)
		cs->long_set[i] = cfg->GetInt(conf, long_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->short_set); i++)
		cs->short_set[i] = cfg->GetInt(conf, short_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->byte_set); i++)
		cs->byte_set[i] = cfg->GetInt(conf, byte_names[i], NULL, 0);
	for (i = 0; i < COUNT(cs->prizeweight_set); i++)
	{
		cs->prizeweight_set[i] = cfg->GetInt(conf,
				prizeweight_names[i], NULL, 0);
		ad->pwps[i] = (total += cs->prizeweight_set[i]);
	}

	/* the funky ones */
	cs->long_set[0] *= 1000; /* BulletDamageLevel */
	cs->long_set[1] *= 1000; /* BombDamageLevel */
	cs->long_set[10] *= 1000; /* BurstDamageLevel */
	cs->long_set[11] *= 1000; /* BulletDamageUpgrade */
	cs->long_set[16] *= 1000; /* InactiveShrapDamage */
}


/* override keys are two small integers stuffed into an unsigned 32-bit
 * integer. the upper 16 bits are the length in bits, and the lower 16
 * are the offset in bits */

void override_work(Arena *arena, override_key_t key, i32 val, int set)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	int len = (key >> 16) & 0xffffu;
	int offset = key & 0xffffu;
	u32 mask = set ? 0xffffffffu : 0;

	/* don't override type byte */
	if (offset < 8)
		return;

	LOCK();
	if ((offset & 7) == 0 && len == 8)
	{
		/* easy case: write byte */
		offset >>= 3;
		((i8*)ad->overrides)[offset] = val;
		((u8*)ad->overridemask)[offset] = mask;
	}
	else if ((offset & 15) == 0 && len == 16)
	{
		/* easy case: write short */
		offset >>= 4;
		((i16*)ad->overrides)[offset] = val;
		((u16*)ad->overridemask)[offset] = mask;
	}
	else if ((offset & 31) == 0 && len == 32)
	{
		/* easy case: write long */
		offset >>= 5;
		((i32*)ad->overrides)[offset] = val;
		((u32*)ad->overridemask)[offset] = mask;
	}
	/* FIXME: add bit support here */
	else
	{
		lm->Log(L_WARN, "<clientset> illegal override key: %x", key);
	}
	UNLOCK();
}


void Override(Arena *arena, override_key_t key, i32 val)
{
	override_work(arena, key, val, TRUE);
}

void Unoverride(Arena *arena, override_key_t key)
{
	override_work(arena, key, 0, FALSE);
}


override_key_t GetOverrideKey(const char *section, const char *key)
{
#define MAKE_KEY(field, len) ((offsetof(struct ClientSettings, field)) << 3 | ((len) << 16))
	char fullkey[MAXSECTIONLEN+MAXKEYLEN];
	int i, j;

	/* do prizeweights */
	if (strcasecmp(section, "PrizeWeight") == 0)
	{
		for (i = 0; i < COUNT(prizeweight_names); i++)
			/* HACK: that +12 there is kind of sneaky */
			if (strcasecmp(prizeweight_names[i]+12, key) == 0)
				return MAKE_KEY(prizeweight_set[i], 8);
		return 0;
	}

	/* do ships */
	for (i = 0; i < 8; i++)
		if (strcasecmp(ship_names[i], section) == 0)
		{
			/* basic stuff */
			for (j = 0; j < COUNT(ship_long_names); j++)
				if (strcasecmp(ship_long_names[j], section) == 0)
					return MAKE_KEY(ships[i].long_set[j], 32);
			for (j = 0; j < COUNT(ship_short_names); j++)
				if (strcasecmp(ship_short_names[j], section) == 0)
					return MAKE_KEY(ships[i].short_set[j], 16);
			for (j = 0; j < COUNT(ship_byte_names); j++)
				if (strcasecmp(ship_byte_names[j], section) == 0)
					return MAKE_KEY(ships[i].byte_set[j], 8);

			/* don't support weapons bits yet:
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
			*/

			/* don't support the strange bitfield yet:
			memset(&misc, 0, sizeof(misc));
			misc.SeeBombLevel = cfg->GetInt(conf, shipname, "SeeBombLevel", 0);
			misc.DisableFastShooting = cfg->GetInt(conf, shipname,
					"DisableFastShooting", 0);
			misc.Radius = cfg->GetInt(conf, shipname, "Radius", 0);
			ss->short_set[10] = *(unsigned short*)&misc;
			*/

			return 0;
		}

	/* don't support spawn locations yet:
	for (i = 0; i < 4; i++)
	{
		char xname[] = "Team#-X";
		char yname[] = "Team#-Y";
		char rname[] = "Team#-Radius";
		xname[4] = yname[4] = rname[4] = '0' + i;
		cs->spawn_pos[i].x = cfg->GetInt(conf, "Spawn", xname, 0);
		cs->spawn_pos[i].y = cfg->GetInt(conf, "Spawn", yname, 0);
		cs->spawn_pos[i].r = cfg->GetInt(conf, "Spawn", rname, 0);
	}
	*/

	/* need full key for remaining ones: */
	snprintf(fullkey, sizeof(fullkey), "%s:%s", section, key);

	/* do rest of settings */
	for (i = 0; i < COUNT(long_names); i++)
		if (strcasecmp(long_names[i], fullkey) == 0)
			return MAKE_KEY(long_set[i], 32);
	for (i = 0; i < COUNT(short_names); i++)
		if (strcasecmp(short_names[i], fullkey) == 0)
			return MAKE_KEY(short_set[i], 16);
	for (i = 0; i < COUNT(byte_names); i++)
		if (strcasecmp(byte_names[i], fullkey) == 0)
			return MAKE_KEY(byte_set[i], 8);

	/* we don't support overriding bits yet:
	cs->bit_set.ExactDamage = cfg->GetInt(conf, "Bullet", "ExactDamage", 0);
	cs->bit_set.HideFlags = cfg->GetInt(conf, "Spectator", "HideFlags", 0);
	cs->bit_set.NoXRadar = cfg->GetInt(conf, "Spectator", "NoXRadar", 0);
	cs->bit_set.SlowFrameRate = cfg->GetInt(conf, "Misc", "SlowFrameCheck", 0);
	cs->bit_set.DisableScreenshot = cfg->GetInt(conf, "Misc", "DisableScreenshot", 0);
	cs->bit_set.MaxTimerDrift = cfg->GetInt(conf, "Misc", "MaxTimerDrift", 0);
	cs->bit_set.DisableBallThroughWalls = cfg->GetInt(conf, "Misc", "DisableBallThroughWalls", 0);
	cs->bit_set.DisableBallKilling = cfg->GetInt(conf, "Misc", "DisableBallKilling", 0);
	*/

	return 0;
#undef MAKE_KEY
}


local void do_mask(struct ClientSettings *dest, adata *ad)
{
	int i;
	unsigned long *c = (unsigned long*)&ad->cs;
	unsigned long *d = (unsigned long*)dest;
	unsigned long *o = (unsigned long*)ad->overrides;
	unsigned long *m = (unsigned long*)ad->overridemask;

	assert((sizeof(*dest) % sizeof(unsigned long)) == 0);
	for (i = 0; i < sizeof(*dest)/sizeof(unsigned long); i++)
		d[i] = (c[i] & ~m[i]) | (o[i] & m[i]);
}


void ActionFunc(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	if (action == AA_CREATE)
	{
		load_settings(ad, arena->cfg);
	}
	else if (action == AA_CONFCHANGED)
	{
		struct ClientSettings tosend;
		load_settings(ad, arena->cfg);
		do_mask(&tosend, ad);
		net->SendToArena(arena, NULL, (byte*)&tosend, sizeof(tosend), NET_RELIABLE);
		lm->LogA(L_INFO, "clientset", arena, "sending modified settings");
	}
	else if (action == AA_DESTROY)
	{
		/* mark settings as destroyed (for asserting later) */
		ad->cs.bit_set.type = 0;
	}
	UNLOCK();
}


void SendClientSettings(Player *p)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	LOCK();
	if (p->arena)
	{
		if (ad->cs.bit_set.type == S2C_SETTINGS)
		{
			struct ClientSettings tosend;
			do_mask(&tosend, ad);
			net->SendToOne(p, (byte*)&tosend, sizeof(tosend), NET_RELIABLE);
		}
		else
			lm->LogA(L_ERROR, "clientset", p->arena, "uninitialized client settings");
	}
	UNLOCK();
}


void Reconfigure(Arena *arena)
{
	struct ClientSettings tosend;
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	load_settings(ad, arena->cfg);
	do_mask(&tosend, ad);
	net->SendToArena(arena, NULL, (byte*)&tosend, sizeof(tosend), NET_RELIABLE);
	UNLOCK();
}


u32 GetChecksum(Arena *arena, u32 key)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	struct ClientSettings tochecksum;
	u32 *data = (u32*)&tochecksum;
	u32 csum = 0, i;
	LOCK();
	do_mask(&tochecksum, ad);
	for (i = 0; i < (SIZE/sizeof(u32)); i++, data++)
		csum += (*data ^ key);
	UNLOCK();
	return csum;
}


int GetRandomPrize(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	int max = ad->pwps[27], r, i = 0, j = 27;

	if (max == 0)
		return 0;

	r = prng->Number(0, max-1);

	/* binary search */
	while (r >= ad->pwps[i])
	{
		int m = (i + j)/2;
		if (r < ad->pwps[m])
			j = m;
		else
			i = m + 1;
	}

	/* our indexes are zero-based but prize numbers are one-based */
	return i + 1;
}

