
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "asss.h"

/* defines */
#define MODULE "flags"

#define LOCK_STATUS(arena) \
	pthread_mutex_lock(flagmtx + arena)
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock(flagmtx + arena)


/* internal structs */
struct MyArenaData
{
	int gametype, minflags, maxflags, currentflags;
	int resetdelay, spawnx, spawny;
	int spawnr, dropr, neutr;
	int friendlytransfer, dropowned, neutowned;
};

/* prototypes */
local void LoadFlagSettings(int arena);
local void SpawnFlag(int arena, int fid);
local void AAFlag(int arena, int action);
local void PAFlag(int pid, int action, int arena);
local void ShipChange(int pid, int ship);

/* timers */
local int BasicFlagTimer(void *);
local int TurfFlagTimer(void *);

/* packet funcs */
local void PPickupFlag(int, byte *, int);
local void PDropFlag(int, byte *, int);

/* interface funcs */
local void MoveFlag(int arena, int fid, int x, int y, int freq);
local void FlagVictory(int arena, int freq, int points);
local void LockFlagStatus(int arena);
local void UnlockFlagStatus(int arena);


/* local data */
local Imodman *mm;
local Inet *net;
local Iconfig *cfg;
local Ilogman *logm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;

/* the big flagdata array */
local struct ArenaFlagData flagdata[MAXARENA];
local struct MyArenaData pflagdata[MAXARENA];
local pthread_mutex_t flagmtx[MAXARENA];

local Iflags _myint =
{ MoveFlag, FlagVictory, LockFlagStatus, UnlockFlagStatus, flagdata };



int MM_flags(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &logm);
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_MAINLOOP, &ml);

		mm->RegCallback(CALLBACK_ARENAACTION, AAFlag, ALLARENAS);
		mm->RegCallback(CALLBACK_PLAYERACTION, PAFlag, ALLARENAS);
		mm->RegCallback(CALLBACK_SHIPCHANGE, ShipChange, ALLARENAS);

		net->AddPacket(C2S_PICKUPFLAG, PPickupFlag);
		net->AddPacket(C2S_DROPFLAGS, PDropFlag);

		{ /* init data */
			int i;
			for (i = 0; i < MAXARENA; i++)
				pflagdata[i].gametype = FLAGGAME_NONE;
		}

		{ /* init mutexes */
			int i;
			pthread_mutexattr_t attr;

			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
			for (i = 0; i < MAXARENA; i++)
				pthread_mutex_init(flagmtx + i, &attr);
			pthread_mutexattr_destroy(&attr);
		}

		/* timers */
		ml->SetTimer(BasicFlagTimer, 500, 500, NULL);
		ml->SetTimer(TurfFlagTimer, 1500, 1500, NULL);

		mm->RegInterface(I_FLAGS, &_myint);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_FLAGS, &_myint);
		mm->UnregCallback(CALLBACK_ARENAACTION, AAFlag, ALLARENAS);
		mm->UnregCallback(CALLBACK_PLAYERACTION, PAFlag, ALLARENAS);
		mm->UnregCallback(CALLBACK_SHIPCHANGE, ShipChange, ALLARENAS);
		net->RemovePacket(C2S_PICKUPFLAG, PPickupFlag);
		net->RemovePacket(C2S_DROPFLAGS, PDropFlag);
		ml->ClearTimer(BasicFlagTimer);
		ml->ClearTimer(TurfFlagTimer);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &logm);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_MAINLOOP, &ml);
		return MM_OK;
	}
	return MM_FAIL;
}



void AAFlag(int arena, int action)
{
	if (action == AA_CREATE)
		LoadFlagSettings(arena);
	else if (action == AA_DESTROY)
		pflagdata[arena].gametype = FLAGGAME_NONE;
}


local void CleanupAfter(int arena, int pid)
{
	/* make sure that if someone leaves, his flags respawn */
	int f;
	LOCK_STATUS(arena);
	if (pflagdata[arena].gametype != FLAGGAME_NONE)
		for (f = 0; f < MAXFLAGS; f++)
			if (flagdata[arena].flags[f].state == FLAG_CARRIED &&
				flagdata[arena].flags[f].carrier == pid)
			{
				flagdata[arena].flags[f].state = FLAG_NEUTED;
				flagdata[arena].flags[f].x = pd->players[pid].position.x>>4;
				flagdata[arena].flags[f].y = pd->players[pid].position.y>>4;
				/* the freq field will be set here. leave it as is. */
			}
	UNLOCK_STATUS(arena);
}

void PAFlag(int pid, int action, int arena)
{
	if (action == PA_LEAVEARENA) CleanupAfter(arena, pid);
}

void ShipChange(int pid, int ship)
{
	if (ship == SPEC) CleanupAfter(pd->players[pid].arena, pid);
}


void LoadFlagSettings(int arena)
{
	char *count, *c2;
	struct MyArenaData d;
	ConfigHandle c = aman->arenas[arena].cfg;

	d.gametype = cfg->GetInt(c, "Flag", "GameType", FLAGGAME_NONE);
	d.resetdelay = cfg->GetInt(c, "Flag", "ResetDelay", 0);
	d.spawnx = cfg->GetInt(c, "Flag", "SpawnX", 512);
	d.spawny = cfg->GetInt(c, "Flag", "SpawnY", 512);
	d.spawnr = cfg->GetInt(c, "Flag", "SpawnRadius", 1024);
	d.dropr = cfg->GetInt(c, "Flag", "DropRadius", 2);
	d.neutr = cfg->GetInt(c, "Flag", "NeutRadius", 2);
	d.friendlytransfer = cfg->GetInt(c, "Flag", "FriendlyTransfer", 1);
	d.dropowned = cfg->GetInt(c, "Flag", "DropOwned", 1);
	d.neutowned = cfg->GetInt(c, "Flag", "NeutOwned", 0);

	count = cfg->GetStr(c, "Flag", "FlagCount");
	if (count)
	{
		d.minflags = strtol(count, NULL, 0);
		if (d.minflags < 0) d.minflags = 0;
		c2 = strchr(count, '-');
		if (c2)
		{
			d.maxflags = strtol(c2+1, NULL, 0);
			if (d.maxflags < d.minflags)
				d.maxflags = d.minflags;
		}
		else
			d.maxflags = d.minflags;
	}
	else
		d.maxflags = d.minflags = 0;

	d.currentflags = 0;

	LOCK_STATUS(arena);
	pflagdata[arena] = d;
	UNLOCK_STATUS(arena);

	logm->Log(L_INFO, "<flags> {%s} Arena has flaggame %d (%d-%d flags)",
			aman->arenas[arena].name,
			d.gametype,
			d.minflags,
			d.maxflags);
}


void SpawnFlag(int arena, int fid)
{
	/* note that this function should be called only for arenas with
	 * FLAGGAME_BASIC */
	int cx, cy, rad, x, y, freq;
	struct FlagData *f = &flagdata[arena].flags[fid];

	LOCK_STATUS(arena);
	if (f->state == FLAG_NONE)
	{
		/* spawning neutral flag in center */
		cx = pflagdata[arena].spawnx;
		cy = pflagdata[arena].spawny;
		rad = pflagdata[arena].spawnr;
		freq = -1;
	}
	else if (f->state == FLAG_CARRIED)
	{
		/* player dropped a flag */
		int pid = f->carrier;
		cx = pd->players[pid].position.x>>4;
		cy = pd->players[pid].position.y>>4;
		rad = pflagdata[arena].dropr;
		if (pflagdata[arena].dropowned)
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
		rad = pflagdata[arena].neutr;
		if (pflagdata[arena].neutowned)
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
	UNLOCK_STATUS(arena);

	do {
		float rndrad, rndang;
		rndrad = (float)rand()/(RAND_MAX+1.0) * (float)rad;
		rndang = (float)rand()/(RAND_MAX+1.0) * M_PI * 2.0;
		x = cx + (rndrad * cos(rndang));
		y = cy + (rndrad * sin(rndang));
		if (x < 0) x = 0;
		if (x > 1023) x = 1023;
		if (y < 0) y = 0;
		if (y > 1023) y = 1023;
	} while (0 /* the square is occupied. need to wait for mapdata */);

	/* whew, finally place the thing */
	MoveFlag(arena, fid, x, y, freq);
}


#include "packets/flags.h"


void MoveFlag(int arena, int fid, int x, int y, int freq)
{
	struct S2CFlagLocation fl = { S2C_FLAGLOC, fid, x, y, freq };

	LOCK_STATUS(arena);
	flagdata[arena].flags[fid].state = FLAG_ONMAP;
	flagdata[arena].flags[fid].x = x;
	flagdata[arena].flags[fid].y = y;
	flagdata[arena].flags[fid].freq = freq;
	flagdata[arena].flags[fid].carrier = -1;
	UNLOCK_STATUS(arena);
	net->SendToArena(arena, -1, (byte*)&fl, sizeof(fl), NET_RELIABLE);
	{ /* do callbacks */
		LinkedList *lst = mm->LookupCallback(CALLBACK_FLAGPOS, arena);
		Link *l;
		for (l = LLGetHead(lst); l; l = l->next)
			((FlagPosFunc)l->data)(arena, fid, x, y, freq);
	}
	logm->Log(L_DRIVEL, "<flags> {%s} Flag %d is at (%d, %d) owned by %d",
			aman->arenas[arena].name, fid, x, y, freq);
}


void FlagVictory(int arena, int freq, int points)
{
	int i;
	struct S2CFlagVictory fv = { S2C_FLAGRESET, freq, points };

	LOCK_STATUS(arena);
	pflagdata[arena].currentflags = 0;
	for (i = 0; i < MAXFLAGS; i++)
		flagdata[arena].flags[i].state = FLAG_NONE;
	UNLOCK_STATUS(arena);

	net->SendToArena(arena, -1, (byte*)&fv, sizeof(fv), NET_RELIABLE);

	{ /* do callbacks */
		LinkedList *lst = mm->LookupCallback(CALLBACK_FLAGWIN, arena);
		Link *l;
		for (l = LLGetHead(lst); l; l = l->next)
			((FlagWinFunc)l->data)(arena, freq);
	}
}


void LockFlagStatus(int arena)
{
	LOCK_STATUS(arena);
}

void UnlockFlagStatus(int arena)
{
	UNLOCK_STATUS(arena);
}


void PPickupFlag(int pid, byte *p, int len)
{
	int arena, oldfreq;
	struct S2CFlagPickup sfp = { S2C_FLAGPICKUP };
	struct FlagData fd;
	struct C2SFlagPickup *cfp = (struct C2SFlagPickup*)p;

	arena = pd->players[pid].arena;

	if (len != sizeof(struct C2SFlagPickup))
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Bad size for flag pickup packet", pd->players[pid].name);
		return;
	}

	if (arena < 0 || arena >= MAXARENA || pd->players[pid].status != S_PLAYING)
	{
		logm->Log(L_WARN, "<flags> [%s] Flag pickup packet from bad arena or status", pd->players[pid].name);
		return;
	}

	if (pd->players[pid].shiptype >= SPEC)
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Flag pickup packet from spec", pd->players[pid].name);
		return;
	}

	LOCK_STATUS(arena);
	/* copy the fd struct so we can modify it */
	fd = flagdata[arena].flags[cfp->fid];
	oldfreq = fd.freq;

	switch (pflagdata[arena].gametype)
	{
		case FLAGGAME_BASIC:
			/* in this game, flags are carried */
			fd.state = FLAG_CARRIED;
			fd.x = -1; fd.y = -1;
			/* we set freq because in case the flag is neuted, that
			 * information is hard to regain */
			fd.freq = pd->players[pid].freq;
			fd.carrier = pid;
			flagdata[arena].flags[cfp->fid] = fd;
			break;

		case FLAGGAME_TURF:
			/* in this game, flags aren't carried. we just have to
			 * change ownership */
			fd.state = FLAG_ONMAP;
			fd.freq = pd->players[pid].freq;
			flagdata[arena].flags[cfp->fid] = fd;
			break;

		case FLAGGAME_NONE:
		case FLAGGAME_CUSTOM:
			/* in both of these, we don't touch flagdata at all. */
	}
	UNLOCK_STATUS(arena);

	/* send packet */
	sfp.fid = cfp->fid;
	sfp.pid = pid;
	net->SendToArena(arena, -1, (byte*)&sfp, sizeof(sfp), NET_RELIABLE);

	/* now call callbacks */
	{
		LinkedList *lst = mm->LookupCallback(CALLBACK_FLAGPICKUP, arena);
		Link *l;
		for (l = LLGetHead(lst); l; l = l->next)
			((FlagPickupFunc)l->data)(arena, pid, cfp->fid, oldfreq);
	}

	logm->Log(L_DRIVEL, "<flags> {%s} [%s] Player picked up flag %d",
			aman->arenas[arena].name,
			pd->players[pid].name,
			cfp->fid);
}


void PDropFlag(int pid, byte *p, int len)
{
	int arena, fid;
	struct S2CFlagDrop sfd = { S2C_FLAGDROP };
	struct FlagData *fd;

	arena = pd->players[pid].arena;

	if (arena < 0 || arena >= MAXARENA || pd->players[pid].status != S_PLAYING)
	{
		logm->Log(L_WARN, "<flags> [%s] Flag drop packet from bad arena or status", pd->players[pid].name);
		return;
	}

	if (pd->players[pid].shiptype >= SPEC)
	{
		logm->Log(L_MALICIOUS, "<flags> [%s] Flag drop packet from spec", pd->players[pid].name);
		return;
	}

	/* send drop packet */
	sfd.pid = pid;
	net->SendToArena(arena, -1, (byte*)&sfd, sizeof(sfd), NET_RELIABLE);

	LOCK_STATUS(arena);

	/* now modify flag info and place flags */
	switch (pflagdata[arena].gametype)
	{
		case FLAGGAME_BASIC:
			/* here, we have to place carried flags */
			for (fid = 0, fd = flagdata[arena].flags; fid < MAXFLAGS; fid++, fd++)
				if (fd->state == FLAG_CARRIED &&
				    fd->carrier == pid)
					SpawnFlag(arena, fid);
			break;

		case FLAGGAME_TURF:
			/* clients shouldn't send this packet in turf games */
			logm->Log(L_MALICIOUS, "<flags> {%s} [%s] Recvd flag drop packet in turf game",
					aman->arenas[arena].name,
					pd->players[pid].name);
			break;

		case FLAGGAME_NONE:
		case FLAGGAME_CUSTOM:
			/* in both of these, we don't touch flagdata at all. */
	}

	UNLOCK_STATUS(arena);

	/* finally call callbacks */
	{
		LinkedList *lst = mm->LookupCallback(CALLBACK_FLAGDROP, arena);
		Link *l;
		for (l = LLGetHead(lst); l; l = l->next)
			((FlagDropFunc)l->data)(arena, pid);
	}

	logm->Log(L_DRIVEL, "<flags> {%s} [%s] Player dropped flags",
			aman->arenas[arena].name,
			pd->players[pid].name);
}


int BasicFlagTimer(void *dummy)
{
	int arena;
	for (arena = 0; arena < MAXARENA; arena++)
	{
		LOCK_STATUS(arena);
		if (pflagdata[arena].gametype == FLAGGAME_BASIC)
		{
			int flagcount, f;
			struct FlagData *flags;

			/* first check if we have to pick a new flag count */
			if (pflagdata[arena].currentflags < pflagdata[arena].minflags)
			{
				float diff, cflags;
				diff = pflagdata[arena].maxflags - pflagdata[arena].minflags;
				cflags = diff * ((float)rand() / (RAND_MAX+1.0));
				pflagdata[arena].currentflags = (int)cflags + pflagdata[arena].minflags;
			}

			/* now check the flags up to flagcount */
			flagcount = pflagdata[arena].currentflags;
			flags = flagdata[arena].flags;
			for (f = 0; f < flagcount; f++)
				if (flags[f].state == FLAG_NONE || flags[f].state == FLAG_NEUTED)
					SpawnFlag(arena, f);
		}
		UNLOCK_STATUS(arena);
	}

	return 1;
}


int TurfFlagTimer(void *dummy)
{
	int arena;
	for (arena = 0; arena < MAXARENA; arena++)
	{
		LOCK_STATUS(arena);
		if (pflagdata[arena].gametype == FLAGGAME_TURF)
		{
			/* send big turf flag summary */
		}
		UNLOCK_STATUS(arena);
	}
	return 1;
}


