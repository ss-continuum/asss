
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "asss.h"

/* extra includes */
#include "packets/flags.h"

#define KEY_TURF_OWNERS 19

#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))


/* internal structs */
typedef struct MyArenaData
{
	int gametype, minflags, maxflags;
	/* min/max flags specify the range of possible flag counts in basic
	 * games. the current flag count is kept in the public array.
	 * maxflags must be equal to the size of the "flags" array in the
	 * public data. */
	int resetdelay, spawnx, spawny;
	int spawnr, dropr, neutr;
	int friendlytransfer, dropowned, neutowned;
	int persistturf;
} MyArenaData;


/* prototypes */
local void SpawnFlag(Arena *arena, int fid);
local void AAFlag(Arena *arena, int action);
local void PAFlag(Player *p, int action, Arena *arena);
local void ShipChange(Player *, int, int);
local void FreqChange(Player *, int);
local void FlagKill(Arena *, Player *, Player *, int, int);

/* timers */
local int BasicFlagTimer(void *);
local int TurfFlagTimer(void *);

/* packet funcs */
local void PPickupFlag(Player *, byte *, int);
local void PDropFlag(Player *, byte *, int);

/* interface funcs */
local void MoveFlag(Arena *arena, int fid, int x, int y, int freq);
local void FlagVictory(Arena *arena, int freq, int points);
local struct ArenaFlagData * GetFlagData(Arena *arena);
local void ReleaseFlagData(Arena *arena);
local int GetCarriedFlags(Player *p);
local int GetFreqFlags(Arena *arena, int freq);


/* local data */
local Imodman *mm;
local Inet *net;
local Iconfig *cfg;
local Ilogman *logm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;
local Imapdata *mapdata;
local Ipersist *persist;

/* the big flagdata array */
local int afdkey, pfdkey, mtxkey;
local ArenaPersistentData persist_turf_owners;

local Iflags _myint =
{
	INTERFACE_HEAD_INIT(I_FLAGS, "flag-core")
	MoveFlag, FlagVictory, GetCarriedFlags, GetFreqFlags,
	GetFlagData, ReleaseFlagData
};



EXPORT int MM_flags(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		logm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);

		afdkey = aman->AllocateArenaData(sizeof(struct FlagData));
		pfdkey = aman->AllocateArenaData(sizeof(struct MyArenaData));
		mtxkey = aman->AllocateArenaData(sizeof(pthread_mutex_t));
		if (afdkey == -1 || pfdkey == -1 || mtxkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, AAFlag, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, PAFlag, ALLARENAS);
		mm->RegCallback(CB_SHIPCHANGE, ShipChange, ALLARENAS);
		mm->RegCallback(CB_FREQCHANGE, FreqChange, ALLARENAS);
		mm->RegCallback(CB_KILL, FlagKill, ALLARENAS);

		net->AddPacket(C2S_PICKUPFLAG, PPickupFlag);
		net->AddPacket(C2S_DROPFLAGS, PDropFlag);

		/* timers */
		ml->SetTimer(BasicFlagTimer, 500, 500, NULL, NULL);
		ml->SetTimer(TurfFlagTimer, 1500, 1500, NULL, NULL);

		if (persist)
			persist->RegArenaPD(&persist_turf_owners);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		if (persist)
			persist->UnregArenaPD(&persist_turf_owners);
		mm->UnregCallback(CB_ARENAACTION, AAFlag, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, PAFlag, ALLARENAS);
		mm->UnregCallback(CB_SHIPCHANGE, ShipChange, ALLARENAS);
		mm->UnregCallback(CB_FREQCHANGE, FreqChange, ALLARENAS);
		mm->UnregCallback(CB_KILL, FlagKill, ALLARENAS);
		net->RemovePacket(C2S_PICKUPFLAG, PPickupFlag);
		net->RemovePacket(C2S_DROPFLAGS, PDropFlag);
		ml->ClearTimer(BasicFlagTimer, NULL);
		ml->ClearTimer(TurfFlagTimer, NULL);
		aman->FreeArenaData(afdkey);
		aman->FreeArenaData(pfdkey);
		aman->FreeArenaData(mtxkey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(logm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(persist);
		return MM_OK;
	}
	return MM_FAIL;
}




void SpawnFlag(Arena *arena, int fid)
{
	/* note that this function should be called only for arenas with
	 * FLAGGAME_BASIC */
	int cx, cy, rad, x, y, freq, good;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	struct FlagData *f = &afd->flags[fid];

	LOCK_STATUS(arena);
	if (f->state == FLAG_NONE)
	{
		/* spawning neutral flag in center */
		cx = pfd->spawnx;
		cy = pfd->spawny;
		rad = pfd->spawnr;
		freq = -1;
	}
	else if (f->state == FLAG_CARRIED)
	{
		/* player dropped a flag */
		Player *p = f->carrier;
		cx = p->position.x>>4;
		cy = p->position.y>>4;
		rad = pfd->dropr;
		if (pfd->dropowned)
			freq = f->freq;
		else
			freq = -1;
	}
	else if (f->state == FLAG_NEUTED)
	{
		/* player specced or left, flag is neuted */
		/* these fields were set when the flag was neuted */
		cx = f->x;
		cy = f->y;
		rad = pfd->neutr;
		if (pfd->neutowned)
			freq = f->freq;
		else
			freq = -1;
	}
	else
	{
		logm->Log(L_WARN, "<flags> SpawnFlag called for a flag on the map");
		UNLOCK_STATUS(arena);
		return;
	}

	do {
		int i, fc;
		double rndrad, rndang;
		/* pick random point */
		rndrad = (double)rand()/(RAND_MAX+1.0) * (double)rad;
		rndang = (double)rand()/(RAND_MAX+1.0) * M_PI * 2.0;
		x = cx + (rndrad * cos(rndang));
		y = cy + (rndrad * sin(rndang));
		/* wrap around, don't clip, so radii of 2048 from a corner
		 * work properly. */
		for (;;)
		{
			if (x < 0)
				x = -x;
			else if (x > 1023)
				x = 2047 - x;
			else
				break;
		}
		for (;;)
		{
			if (y < 0)
				y = -y;
			else if (y > 1023)
				y = 2047 - y;
			else
				break;
		}

		/* ask mapdata to move it to nearest empty tile */
		mapdata->FindFlagTile(arena, &x, &y);

		/* finally make sure it doesn't hit any other flags */
		good = 1;
		fc = pfd->maxflags;
		for (i = 0, f = afd->flags; i < fc; i++, f++)
			if (f->state == FLAG_ONMAP && /* only check placed flags */
			    fid != i && /* don't compare with ourself */
			    f->x == x &&
			    f->y == y)
				good = 0;

		/* if it did hit another flag, bump the radius a bit so we don't
		 * get stuck here */
		if (!good) rad++;
	} while (!good);

	/* whew, finally place the thing */
	MoveFlag(arena, fid, x, y, freq);

	UNLOCK_STATUS(arena);
}


void MoveFlag(Arena *arena, int fid, int x, int y, int freq)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	struct S2CFlagLocation fl = { S2C_FLAGLOC, fid, x, y, freq };
	Player *oldp;

	LOCK_STATUS(arena);
	afd->flags[fid].state = FLAG_ONMAP;
	afd->flags[fid].x = x;
	afd->flags[fid].y = y;
	afd->flags[fid].freq = freq;
	oldp = afd->flags[fid].carrier;
	if (oldp &&
	    oldp->status == S_PLAYING &&
	    oldp->pkt.flagscarried > 0)
		oldp->pkt.flagscarried--;
	afd->flags[fid].carrier = NULL;
	UNLOCK_STATUS(arena);

	net->SendToArena(arena, NULL, (byte*)&fl, sizeof(fl), NET_RELIABLE);
	DO_CBS(CB_FLAGPOS, arena, FlagPosFunc,
			(arena, fid, x, y, freq));
	logm->Log(L_DRIVEL, "<flags> {%s} Flag %d is at (%d, %d) owned by %d",
			arena->name, fid, x, y, freq);
}


void FlagVictory(Arena *arena, int freq, int points)
{
	int i, fc;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	struct S2CFlagVictory fv = { S2C_FLAGRESET, freq, points };

	LOCK_STATUS(arena);
	afd->flagcount = 0;
	fc = pfd->maxflags;
	for (i = 0; i < fc; i++)
		afd->flags[i].state = FLAG_NONE;
	UNLOCK_STATUS(arena);

	net->SendToArena(arena, NULL, (byte*)&fv, sizeof(fv), NET_RELIABLE);
}


ArenaFlagData * GetFlagData(Arena *arena)
{
	LOCK_STATUS(arena);
	return P_ARENA_DATA(arena, afdkey);
}

void ReleaseFlagData(Arena *arena)
{
	UNLOCK_STATUS(arena);
}

int GetCarriedFlags(Player *p)
{
	Arena *arena = p->arena;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	int tot = 0, i, fc;
	struct FlagData *f;

	if (!arena) return -1;

	LOCK_STATUS(arena);
	f = afd->flags;
	fc = afd->flagcount;
	for (i = 0; i < fc; i++, f++)
		if (f->state == FLAG_CARRIED &&
		    f->carrier == p)
			tot++;
	UNLOCK_STATUS(arena);
	return tot;
}

int GetFreqFlags(Arena *arena, int freq)
{
	int tot = 0, i, fc;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	struct FlagData *f;

	if (!arena) return -1;

	f = afd->flags;
	LOCK_STATUS(arena);
	fc = afd->flagcount;
	for (i = 0; i < fc; i++, f++)
		if ( ( f->state == FLAG_ONMAP &&
		       f->freq == freq ) ||
		     ( f->state == FLAG_CARRIED &&
		       f->carrier &&
		       f->carrier->p_freq == freq ) )
			tot++;
	UNLOCK_STATUS(arena);
	return tot;
}


local void LoadFlagSettings(Arena *arena, int init)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	ConfigHandle c = arena->cfg;

	/* get flag game type, only the first time */
	/* cfghelp: Flag:GameType, arena, enum, def: $FLAGGAME_NONE
	 * The flag game type for this arena. $FLAGGAME_NONE means no flag
	 * game, $FLAGGAME_BASIC is a standard warzone or running zone game,
	 * and $FLAGGAME_TURF specifies immobile flags. */
	if (init)
		pfd->gametype = cfg->GetInt(c, "Flag", "GameType", FLAGGAME_NONE);

	/* and initialize settings for that type */
	if (pfd->gametype == FLAGGAME_BASIC)
	{
		const char *count, *c2;

		/* cfghelp: Flag:ResetDelay, arena, int, def: 0
		 * The length of the delay between flag games. */
		pfd->resetdelay = cfg->GetInt(c, "Flag", "ResetDelay", 0);
		/* cfghelp: Flag:SpawnX, arena, int, def: 512
		 * The X coordinate that new flags spawn at (in tiles). */
		pfd->spawnx = cfg->GetInt(c, "Flag", "SpawnX", 512);
		/* cfghelp: Flag:SpawnY, arena, int, def: 512
		 * The Y coordinate that new flags spawn at (in tiles). */
		pfd->spawny = cfg->GetInt(c, "Flag", "SpawnY", 512);
		/* cfghelp: Flag:SpawnRadius, arena, int, def: 50
		 * How far from the spawn center that new flags spawn (in
		 * tiles). */
		pfd->spawnr = cfg->GetInt(c, "Flag", "SpawnRadius", 50);
		/* cfghelp: Flag:DropRadius, arena, int, def: 2
		 * How far from a player do dropped flags appear (in tiles). */
		pfd->dropr = cfg->GetInt(c, "Flag", "DropRadius", 2);
		/* cfghelp: Flag:NeutRadius, arena, int, def: 2
		 * How far from a player do neut-dropped flags appear (in
		 * tiles). */
		pfd->neutr = cfg->GetInt(c, "Flag", "NeutRadius", 2);
		/* cfghelp: Flag:FriendlyTransfer , arena, bool, def: 1
		 * Whether you get a teammates flags when you kill him. */
		pfd->friendlytransfer = cfg->GetInt(c, "Flag", "FriendlyTransfer", 1);
		/* cfghelp: Flag:DropOwned, arena, bool, def: 1
		 * Whether flags you drop are owned by your team. */
		pfd->dropowned = cfg->GetInt(c, "Flag", "DropOwned", 1);
		/* cfghelp: Flag:NeutOwned, arena, bool, def: 0
		 * Whether flags you neut-drop are owned by your team. */
		pfd->neutowned = cfg->GetInt(c, "Flag", "NeutOwned", 0);

		if (init)
		{
			/* cfghelp: Flag:FlagCount, arena, rng, range: 0-256, def: 0
			 * How many flags are present in this arena. */
			count = cfg->GetStr(c, "Flag", "FlagCount");
			if (count)
			{
				pfd->minflags = strtol(count, NULL, 0);
				if (pfd->minflags < 0) pfd->minflags = 0;
				if (pfd->minflags > 256) pfd->minflags = 256;
				c2 = strchr(count, '-');
				if (c2)
				{
					pfd->maxflags = strtol(c2+1, NULL, 0);
					if (pfd->maxflags > 256) pfd->maxflags = 256;
					if (pfd->maxflags < pfd->minflags)
						pfd->maxflags = pfd->minflags;
				}
				else
					pfd->maxflags = pfd->minflags;
			}
			else
				pfd->maxflags = pfd->minflags = 0;

			/* allocate array for public flag data */
			afd->flags = amalloc(pfd->maxflags * sizeof(struct FlagData));

			/* the timer event will notice that flagcount < maxflags and
			 * init the flags. */
			afd->flagcount = 0;
		}
	}
	else if (pfd->gametype == FLAGGAME_TURF && init)
	{
		int i;
		struct FlagData *f;

		/* cfghelp: Flag:PersistentTurfOwners, arena, bool, def: 1
		 * Whether ownership of turf flags persists even when the arena
		 * is empty (or the server crashes). */
		pfd->persistturf = cfg->GetInt(c, "Flag", "PersistentTurfOwners", 1);

		pfd->minflags = pfd->maxflags = afd->flagcount =
			mapdata->GetFlagCount(arena);

		/* allocate array for public flag data */
		afd->flags = amalloc(pfd->maxflags * sizeof(struct FlagData));

		for (i = 0, f = afd->flags; i < pfd->maxflags; i++, f++)
		{
			f->state = FLAG_ONMAP;
			f->freq = -1;
			f->x = -1;
			f->y = -1;
		}
	}

	if (init && pfd->gametype)
		logm->Log(L_INFO, "<flags> {%s} Arena has flaggame %d (%d-%d flags)",
				arena->name,
				pfd->gametype,
				pfd->minflags,
				pfd->maxflags);
}


void AAFlag(Arena *arena, int action)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);

	if (action == AA_CREATE)
	{
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);
		memset(afd, 0, sizeof(*afd));
		memset(pfd, 0, sizeof(*pfd));
	}
	else if (action == AA_DESTROY)
		pthread_mutex_destroy(P_ARENA_DATA(arena, mtxkey));

	if (action == AA_CREATE || action == AA_DESTROY) {
		/* clean up old flag data */
		if (afd->flags)
		{
			afree(afd->flags);
			afd->flags = NULL;
			afd->flagcount = 0;
		}
		pfd->gametype = FLAGGAME_NONE;
	}

	LOCK_STATUS(arena);
	if (action == AA_CREATE)
	{
		/* only if we're creating, load the data */
		LoadFlagSettings(arena, 1);
	}
	else if (action == AA_CONFCHANGED)
	{
		/* load settings, but don't spawn flags */
		LoadFlagSettings(arena, 0);
	}
	UNLOCK_STATUS(arena);
}


local void CheckWin(Arena *arena)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	struct FlagData *f;
	int freq = -1, flagc = 0, i, cflags, fc;

	/* figure out how many flags are owned by some freq that owns flags.
	 * note that we don't need the max, because by definition, a win is
	 * when you're the _only_ freq that owns flags. */
	LOCK_STATUS(arena);
	cflags = afd->flagcount;
	fc = pfd->maxflags;
	for (i = 0, f = afd->flags; i < fc; i++, f++)
		if (f->state == FLAG_ONMAP)
		{
			if (f->freq == freq)
				flagc++;
			else
			{
				freq = f->freq;
				flagc = 1;
			}
		}
	UNLOCK_STATUS(arena);

	if (freq != -1 && flagc == cflags)
	{
		/* signal a win by calling callbacks. they should at least call
		 * flags->FlagVictory to reset the game. */
		DO_CBS(CB_FLAGWIN, arena, FlagWinFunc,
				(arena, freq));

		logm->Log(L_INFO, "<flags> {%s} Flag victory: freq %d won",
				arena->name, freq);
	}

}


local void CleanupAfter(Arena *arena, Player *p)
{
	/* make sure that if someone leaves, his flags respawn */
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	int i, fc, dropped = 0;
	struct FlagData *f = afd->flags;

	p->pkt.flagscarried = 0;

	LOCK_STATUS(arena);
	fc = pfd->maxflags;
	if (pfd->gametype != FLAGGAME_NONE)
		for (i = 0; i < fc; i++, f++)
			if (f->state == FLAG_CARRIED &&
				f->carrier == p)
			{
				f->state = FLAG_NEUTED;
				f->x = p->position.x>>4;
				f->y = p->position.y>>4;
				/* the freq field will be set here. leave it as is. */
				dropped++;
			}
	UNLOCK_STATUS(arena);

	if (dropped)
		DO_CBS(CB_FLAGDROP, arena, FlagDropFunc, (arena, p, dropped, 1));
}


void PAFlag(Player *p, int action, Arena *arena)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);

	if (action == PA_ENTERARENA)
	{
		/* send him flag locations */
		int i, fc;
		struct FlagData *f = afd->flags;

		LOCK_STATUS(arena);
		fc = afd->flagcount;
		if (pfd->gametype == FLAGGAME_BASIC)
			for (i = 0; i < fc; i++, f++)
			{
				if (f->state == FLAG_ONMAP)
				{
					struct S2CFlagLocation fl = { S2C_FLAGLOC, i, f->x, f->y, f->freq };
					net->SendToOne(p, (byte*)&fl, sizeof(fl), NET_RELIABLE);
				}
				else if (f->state == FLAG_CARRIED && f->carrier)
				{
					struct S2CFlagPickup fp = { S2C_FLAGPICKUP, i, f->carrier->pid};
					net->SendToOne(p, (byte*)&fp, sizeof(fp), NET_RELIABLE);
				}
			}
		UNLOCK_STATUS(arena);
		p->pkt.flagscarried = 0;
	}
	else if (action == PA_LEAVEARENA)
		CleanupAfter(arena, p);
}


void ShipChange(Player *p, int ship, int newfreq)
{
	CleanupAfter(p->arena, p);
}

void FreqChange(Player *p, int newfreq)
{
	CleanupAfter(p->arena, p);
}

void FlagKill(Arena *arena, Player *killer, Player *killed, int bounty, int flags)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	int i, fc, newfreq;
	struct FlagData *f;

	if (flags < 1) return;

	f = afd->flags;
	newfreq = killer->p_freq;
	LOCK_STATUS(arena);
	fc = pfd->maxflags;
	if (killer->p_freq != killed->p_freq ||
	    pfd->friendlytransfer)
	{
		for (i = 0; i < fc; i++, f++)
		{
			if (f->state == FLAG_CARRIED &&
				f->carrier == killed)
			{
				f->carrier = killer;
				f->freq = newfreq;
				killer->pkt.flagscarried++;
			}
		}
	}
	else
	{
		/* friendlytransfer is off. we have to drop the flags from the
		 * killer and spawn them. */
		struct S2CFlagDrop sfd = { S2C_FLAGDROP, killer->pid };
		net->SendToArena(arena, NULL, (byte*)&sfd, sizeof(sfd), NET_RELIABLE);

		for (i = 0; i < fc; i++, f++)
		{
			if (f->state == FLAG_CARRIED &&
				f->carrier == killed)
				SpawnFlag(arena, i);
		}
	}
	UNLOCK_STATUS(arena);
	killed->pkt.flagscarried = 0;
	/* logm->Log(L_DRIVEL, "<flags> [%s] by [%s] Flag kill: %d flags transferred",
			killed->name,
			killer->name,
			tot); */
}


void PPickupFlag(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	int oldfreq, carried;
	struct S2CFlagPickup sfp = { S2C_FLAGPICKUP };
	struct FlagData fd;
	struct C2SFlagPickup *cfp = (struct C2SFlagPickup*)pkt;

	if (!arena)
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Flag pickup from bad arena",
				p->name);
		return;
	}

#define ERR(msg) \
	{ \
		logm->LogP(L_MALICIOUS, "flags", p, msg); \
		return; \
	}

	if (p->status != S_PLAYING)
		ERR("Flag pickup from bad arena or status")

	if (len != sizeof(struct C2SFlagPickup))
		ERR("Bad size for flag pickup packet")

	if (p->p_ship >= SPEC)
		ERR("Flag pickup packet from spec")

	if (p->flags.during_change)
		ERR("Flag pickup before ship/freq change ack")

#undef ERR

	/* this player is too lagged to have a flag */
	if (p->flags.no_flags_balls)
	{
		logm->LogP(L_INFO, "flags", p, "too lagged to pick up flag %d", cfp->fid);
		return;
	}

	LOCK_STATUS(arena);
	/* copy the fd struct so we can modify it */
	fd = afd->flags[cfp->fid];
	oldfreq = fd.freq;

	/* make sure someone else didn't get it first */
	if (fd.state != FLAG_ONMAP)
	{
		logm->Log(L_MALICIOUS, "<flags> {%s} [%s] Tried to pick up a carried flag",
				arena->name,
				p->name);
		UNLOCK_STATUS(arena);
		return;
	}

	switch (pfd->gametype)
	{
		case FLAGGAME_BASIC:
			/* in this game, flags are carried */
			fd.state = FLAG_CARRIED;
			fd.x = -1; fd.y = -1;
			/* we set freq because in case the flag is neuted, that
			 * information is hard to regain */
			fd.freq = p->p_freq;
			fd.carrier = p;
			p->pkt.flagscarried++;

			afd->flags[cfp->fid] = fd;
			break;

		case FLAGGAME_TURF:
			/* in this game, flags aren't carried. we just have to
			 * change ownership */
			fd.state = FLAG_ONMAP;
			fd.freq = p->p_freq;

			afd->flags[cfp->fid] = fd;
			break;

		case FLAGGAME_NONE:
		case FLAGGAME_CUSTOM:
			/* in both of these, we don't touch flagdata at all. */
			break;
	}
	UNLOCK_STATUS(arena);

	/* send packet */
	sfp.fid = cfp->fid;
	sfp.pid = p->pid;
	net->SendToArena(arena, NULL, (byte*)&sfp, sizeof(sfp), NET_RELIABLE);

	/* now call callbacks */
	carried = (afd->flags[cfp->fid].state == FLAG_CARRIED);
	DO_CBS(CB_FLAGPICKUP, arena, FlagPickupFunc,
			(arena, p, cfp->fid, oldfreq, carried));

	logm->Log(L_DRIVEL, "<flags> {%s} [%s] Player picked up flag %d",
			arena->name,
			p->name,
			cfp->fid);
}


void PDropFlag(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	int fid, fc, dropped = 0;
	struct S2CFlagDrop sfd = { S2C_FLAGDROP };
	struct FlagData *fd;

	if (!arena || p->status != S_PLAYING)
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Flag drop packet from bad arena or status", p->name);
		return;
	}

	if (p->p_ship >= SPEC)
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Flag drop packet from spec", p->name);
		return;
	}

	/* send drop packet */
	sfd.pid = p->pid;
	net->SendToArena(arena, NULL, (byte*)&sfd, sizeof(sfd), NET_RELIABLE);

	LOCK_STATUS(arena);
	fc = pfd->maxflags;

	/* now modify flag info and place flags */
	switch (pfd->gametype)
	{
		case FLAGGAME_BASIC:
			/* here, we have to place carried flags */
			for (fid = 0, fd = afd->flags; fid < fc; fid++, fd++)
				if (fd->state == FLAG_CARRIED &&
				    fd->carrier == p)
				{
					SpawnFlag(arena, fid);
					dropped++;
				}
			break;

		case FLAGGAME_TURF:
			/* clients shouldn't send this packet in turf games */
			logm->Log(L_MALICIOUS, "<flags> {%s} [%s] Recvd flag drop packet in turf game",
					arena->name,
					p->name);
			break;

		case FLAGGAME_NONE:
		case FLAGGAME_CUSTOM:
			/* in both of these, we don't touch flagdata at all. */
			break;
	}

	UNLOCK_STATUS(arena);

	/* finally call callbacks */
	DO_CBS(CB_FLAGDROP, arena, FlagDropFunc, (arena, p, dropped, 0));

	logm->Log(L_DRIVEL, "<flags> {%s} [%s] Player dropped flags",
			arena->name,
			p->name);

	/* do this after the drop callbacks have been called because this
	 * has the potential to call the win callbacks and we don't want the
	 * drop ones called after the win ones. */
	CheckWin(arena);
}


int BasicFlagTimer(void *dummy)
{
	Arena *arena;
	Link *link;

	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
		MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);

		LOCK_STATUS(arena);
		if (pfd->gametype == FLAGGAME_BASIC)
		{
			int flagcount, f;
			struct FlagData *flags;

			/* first check if we have to pick a new flag count */
			if (afd->flagcount < pfd->minflags)
			{
				double diff, cflags;
				diff = (double)(pfd->maxflags - pfd->minflags + 1);
				cflags = diff * ((double)rand() / (RAND_MAX+1.0));
				afd->flagcount = (int)cflags + pfd->minflags;
			}

			/* now check the flags up to flagcount */
			flagcount = afd->flagcount;
			flags = afd->flags;
			for (f = 0; f < flagcount; f++)
				if (flags[f].state == FLAG_NONE || flags[f].state == FLAG_NEUTED)
					SpawnFlag(arena, f);

			/* also check wins in case it wasn't due to a drop. it is
			 * possible, if you have NeutOwned on. */
			CheckWin(arena);
		}
		UNLOCK_STATUS(arena);
	}
	aman->Unlock();

	return TRUE;
}


int TurfFlagTimer(void *dummy)
{
	Arena *arena;
	Link *link;

	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
		MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);

		LOCK_STATUS(arena);
		if (pfd->gametype == FLAGGAME_TURF)
		{
			/* send big turf flag summary */
			int i, fc;
			struct SimplePacketA *pkt;

			fc = afd->flagcount;
			pkt = alloca(1 + fc * 2);
			pkt->type = S2C_TURFFLAGS;
			for (i = 0; i < fc; i++)
				pkt->d[i] = afd->flags[i].freq;
			net->SendToArena(arena, NULL, (byte*)pkt, 1 + fc * 2, NET_UNRELIABLE);
		}
		UNLOCK_STATUS(arena);
	}
	aman->Unlock();

	return TRUE;
}


local int get_turf_owners(Arena *arena, void *data, int len)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	short *d = data, i;
	int fc;

	LOCK_STATUS(arena);

	if (pfd->gametype != FLAGGAME_TURF ||
	    !pfd->persistturf ||
	    !afd->flags)
	{
		UNLOCK_STATUS(arena);
		return 0;
	}

	fc = afd->flagcount;
	for (i = 0; i < fc; i++)
		d[i] = afd->flags[i].freq;

	UNLOCK_STATUS(arena);

	return fc * sizeof(short);
}

local void set_turf_owners(Arena *arena, void *data, int len)
{
	ArenaFlagData *afd = P_ARENA_DATA(arena, afdkey);
	MyArenaData *pfd = P_ARENA_DATA(arena, pfdkey);
	short *d = data, i;
	int fc;

	LOCK_STATUS(arena);

	if (pfd->gametype != FLAGGAME_TURF ||
	    !pfd->persistturf ||
	    !afd->flags)
	{
		UNLOCK_STATUS(arena);
		return;
	}

	fc = afd->flagcount;

	if (len != fc * sizeof(short))
	{
		UNLOCK_STATUS(arena);
		return;
	}

	for (i = 0; i < fc; i++)
		afd->flags[i].freq = d[i];

	UNLOCK_STATUS(arena);
}

local void clear_turf_owners(Arena *arena)
{
	/* no-op: the arena create action does this already */
}

local ArenaPersistentData persist_turf_owners =
{
	/* ideally, this would only get registered for arenas with a turf
	 * game going, but this is ok. */
	KEY_TURF_OWNERS, INTERVAL_GAME, PERSIST_ALLARENAS,
	get_turf_owners, set_turf_owners, clear_turf_owners
};

