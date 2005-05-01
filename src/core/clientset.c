
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

typedef struct overridedata
{
	byte bits[SIZE];
	byte mask[SIZE];
} overridedata;

typedef struct adata
{
	struct ClientSettings cs;
	overridedata od;
	/* prizeweight partial sums. 0-27 are used for now, representing
	 * prizes 1 to 28. */
	unsigned short pwps[32];
} adata;

typedef struct pdata
{
	overridedata *od;
} pdata;

/* global data */

local int adkey, pdkey;
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


/* the client settings definition */
#include "clientset.def"


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
	cs->bit_set.DisableBallThroughWalls = cfg->GetInt(conf, "Soccer", "DisableWallPass", 0);
	cs->bit_set.DisableBallKilling = cfg->GetInt(conf, "Soccer", "DisableBallKilling", 0);

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


local override_key_t GetOverrideKey(const char *section, const char *key)
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


/* override keys are two small integers stuffed into an unsigned 32-bit
 * integer. the upper 16 bits are the length in bits, and the lower 16
 * are the offset in bits */
/* call with lock held */
local void override_work(overridedata *od, override_key_t key, i32 val, int set)
{
	int len = (key >> 16) & 0xffffu;
	int offset = key & 0xffffu;
	u32 mask = set ? 0xffffffffu : 0;

	/* don't override type byte */
	if (offset < 8)
		return;

	if ((offset & 7) == 0 && len == 8)
	{
		/* easy case: write byte */
		offset >>= 3;
		((i8*)od->bits)[offset] = val;
		((u8*)od->mask)[offset] = mask;
	}
	else if ((offset & 15) == 0 && len == 16)
	{
		/* easy case: write short */
		offset >>= 4;
		((i16*)od->bits)[offset] = val;
		((u16*)od->mask)[offset] = mask;
	}
	else if ((offset & 31) == 0 && len == 32)
	{
		/* easy case: write long */
		offset >>= 5;
		((i32*)od->bits)[offset] = val;
		((u32*)od->mask)[offset] = mask;
	}
	/* FIXME: add bit support here */
	else
	{
		lm->Log(L_WARN, "<clientset> illegal override key: %x", key);
	}
}


local void ArenaOverride(Arena *arena, override_key_t key, i32 val)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	override_work(&ad->od, key, val, TRUE);
	UNLOCK();
}

local void ArenaUnoverride(Arena *arena, override_key_t key)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	override_work(&ad->od, key, 0, FALSE);
	UNLOCK();
}


local void PlayerOverride(Player *p, override_key_t key, i32 val)
{
	pdata *data = PPDATA(p, pdkey);
	LOCK();
	if (!data->od)
		data->od = amalloc(sizeof(*data->od));
	override_work(data->od, key, val, TRUE);
	UNLOCK();
}

local void PlayerUnoverride(Player *p, override_key_t key)
{
	pdata *data = PPDATA(p, pdkey);
	LOCK();
	if (data->od)
		override_work(data->od, key, 0, FALSE);
	UNLOCK();
}


/* call with lock held */
local void do_mask(
		struct ClientSettings *dest,
		struct ClientSettings *src,
		overridedata *od1,
		overridedata *od2)
{
	int i;
	unsigned long *s = (unsigned long*)src;
	unsigned long *d = (unsigned long*)dest;
	unsigned long *o1 = (unsigned long*)od1->bits;
	unsigned long *m1 = (unsigned long*)od1->mask;

	if (od2)
	{
		unsigned long *o2 = (unsigned long*)od2->bits;
		unsigned long *m2 = (unsigned long*)od2->mask;

		for (i = 0; i < sizeof(*dest)/sizeof(unsigned long); i++)
			d[i] = (((s[i] & ~m1[i]) | (o1[i] & m1[i])) & ~m2[i]) | (o2[i] & m2[i]);
	}
	else
	{
		for (i = 0; i < sizeof(*dest)/sizeof(unsigned long); i++)
			d[i] = (s[i] & ~m1[i]) | (o1[i] & m1[i]);
	}
}


/* call with lock held */
local void send_one_settings(Player *p, adata *ad)
{
	struct ClientSettings tosend;
	pdata *data = PPDATA(p, pdkey);
	do_mask(&tosend, &ad->cs, &ad->od, data->od);
	if (tosend.bit_set.type == S2C_SETTINGS)
		net->SendToOne(p, (byte*)&tosend, sizeof(tosend), NET_RELIABLE);
}


local void aaction(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	LOCK();
	if (action == AA_CREATE)
	{
		load_settings(ad, arena->cfg);
	}
	else if (action == AA_CONFCHANGED)
	{
		struct ClientSettings old;
		memcpy(&old, &ad->cs, SIZE);
		load_settings(ad, arena->cfg);
		if (memcmp(&old, &ad->cs, SIZE) != 0)
		{
			Player *p;
			Link *link;
			lm->LogA(L_INFO, "clientset", arena, "sending modified settings");
			FOR_EACH_PLAYER(p)
				if (p->arena == arena && p->status == S_PLAYING)
					send_one_settings(p, ad);
		}
	}
	else if (action == AA_DESTROY)
	{
		/* mark settings as destroyed (for asserting later) */
		ad->cs.bit_set.type = 0;
	}
	UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_LEAVEARENA || action == PA_DISCONNECT)
	{
		/* reset/free player overrides on any arena change */
		pdata *data = PPDATA(p, pdkey);
		afree(data->od);
		data->od = NULL;
	}
}


local void SendClientSettings(Player *p)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (!p->arena)
		return;
	LOCK();
	send_one_settings(p, ad);
	UNLOCK();
}


local u32 GetChecksum(Player *p, u32 key)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	struct ClientSettings tochecksum;
	u32 *bits = (u32*)&tochecksum;
	u32 csum = 0;
	int i;

	if (p->status != S_PLAYING)
		return -1;

	LOCK();
	do_mask(&tochecksum, &ad->cs, &ad->od, data->od);
	for (i = 0; i < (SIZE/sizeof(u32)); i++, bits++)
		csum += (*bits ^ key);
	UNLOCK();

	return csum;
}


local int GetRandomPrize(Arena *arena)
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


local Iclientset csint =
{
	INTERFACE_HEAD_INIT(I_CLIENTSET, "clientset")
	SendClientSettings, GetChecksum, GetRandomPrize,
	GetOverrideKey,
	ArenaOverride, ArenaUnoverride,
	PlayerOverride, PlayerUnoverride,
};


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
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		mm->RegInterface(&csint, ALLARENAS);

		/* do these at least once */
#define cs (*((struct ClientSettings*)0))
#define ss (*((struct ShipSettings*)0))
		assert((sizeof(cs) % sizeof(unsigned long)) == 0);
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
		if (mm->UnregInterface(&csint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
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

