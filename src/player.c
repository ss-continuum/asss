
/* dist: public */

#include <string.h>
#include <assert.h>

#include "rwlock.h"

#include "asss.h"

#define USE_RWLOCK

/* static data */

local Imodman *mm;
local int dummykey, magickey, mtxkey;
#ifdef USE_RWLOCK
local rwlock_t plock;
#define RDLOCK() rwl_readlock(&plock)
#define WRLOCK() rwl_writelock(&plock)
#define RULOCK() rwl_readunlock(&plock)
#define WULOCK() rwl_writeunlock(&plock)
#else
local pthread_mutex_t plock;
#define RDLOCK() pthread_mutex_lock(&plock)
#define WRLOCK() pthread_mutex_lock(&plock)
#define RULOCK() pthread_mutex_unlock(&plock)
#define WULOCK() pthread_mutex_unlock(&plock)
#endif

#define pd (&pdint)

local Player **pidmap;
local int pidmapsize;
local int perplayerspace;
#ifdef CFG_DYNAMIC_PPD
local pthread_mutex_t extradatamtx;
#endif
local pthread_mutexattr_t recmtxattr;

/* forward declaration */
local Iplayerdata pdint;


local void LockPlayer(Player *p)
{
	assert(*(unsigned*)PPDATA(p, magickey) == MODMAN_MAGIC);
	pthread_mutex_lock((pthread_mutex_t*)PPDATA(p, mtxkey));
}

local void UnlockPlayer(Player *p)
{
	pthread_mutex_unlock((pthread_mutex_t*)PPDATA(p, mtxkey));
}

local void Lock(void)
{
	RDLOCK();
}

local void WriteLock(void)
{
	WRLOCK();
}

local void Unlock(void)
{
	RULOCK();
}

local void WriteUnlock(void)
{
	WULOCK();
}


local Player * NewPlayer(int type)
{
	int pid;
#ifdef CFG_DYNAMIC_PPD
	Player *p = amalloc(sizeof(*p));
	pthread_mutex_lock(&extradatamtx);
	p->playerextradata = amalloc(perplayerspace);
	pthread_mutex_unlock(&extradatamtx);
#else
	Player *p = amalloc(sizeof(*p) + perplayerspace);
#endif

	*(unsigned*)PPDATA(p, magickey) = MODMAN_MAGIC;
	pthread_mutex_init((pthread_mutex_t*)PPDATA(p, mtxkey), &recmtxattr);

	WRLOCK();
	/* find a free pid */
	for (pid = 0; pidmap[pid] != NULL && pid < pidmapsize; pid++) ;

	if (pid == pidmapsize)
	{
		/* no more pids left */
		int newsize = pidmapsize * 2;
		pidmap = arealloc(pidmap, newsize * sizeof(Player*));
		for (pid = pidmapsize; pid < newsize; pid++)
			pidmap[pid] = 0;
		pid = pidmapsize;
		pidmapsize = newsize;
	}

	pidmap[pid] = p;
	LLAdd(&pd->playerlist, p);
	WULOCK();

	/* set up player struct and packet */
	p->pkt.pktype = S2C_PLAYERENTERING;
	p->pkt.pid = pid;
	p->status = S_CONNECTED;
	p->type = type;
	p->arena = NULL;
	p->newarena = NULL;
	p->pid = pid;
	p->p_ship = SHIP_SPEC;
	p->p_attached = -1;
	p->connecttime = current_ticks();
	p->connectas = NULL;

	DO_CBS(CB_NEWPLAYER, ALLARENAS, NewPlayerFunc, (p, TRUE));

	return p;
}


local void FreePlayer(Player *p)
{
	DO_CBS(CB_NEWPLAYER, ALLARENAS, NewPlayerFunc, (p, FALSE));

	WRLOCK();
	LLRemove(&pd->playerlist, p);
	pidmap[p->pid] = NULL;
	WULOCK();

	pthread_mutex_destroy((pthread_mutex_t*)PPDATA(p, mtxkey));

#ifdef CFG_DYNAMIC_PPD
	afree(p->playerextradata);
#endif

	afree(p);
}


local Player * PidToPlayer(int pid)
{
	RDLOCK();
	if (pid >= 0 && pid < pidmapsize)
	{
		Player *p = pidmap[pid];
		RULOCK();
		return p;
	}
	RULOCK();
	return NULL;
}


local void KickPlayer(Player *p)
{
	if (IS_STANDARD(p))
	{
		Inet *net = mm->GetInterface(I_NET, ALLARENAS);
		if (net)
			net->DropClient(p);
		mm->ReleaseInterface(net);
	}
	else if (IS_CHAT(p))
	{
		Ichatnet *chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		if (chatnet)
			chatnet->DropClient(p);
		mm->ReleaseInterface(chatnet);
	}
}


local Player * FindPlayer(const char *name)
{
	Link *link;
	Player *p;

	RDLOCK();
	FOR_EACH_PLAYER(p)
		if (strcasecmp(name, p->name) == 0 &&
			/* this is a sort of hackish way of not returning
			 * players who are on their way out. */
		    p->status < S_LEAVING_ZONE &&
		    p->whenloggedin < S_LEAVING_ZONE)
		{
			RULOCK();
			return p;
		}
	RULOCK();
	return NULL;
}


local inline int matches(const Target *t, Player *p)
{
	switch (t->type)
	{
		case T_NONE:
			return 0;

		case T_ARENA:
			return p->arena == t->u.arena;

		case T_FREQ:
			return p->arena == t->u.freq.arena &&
			       p->p_freq == t->u.freq.freq;

		case T_ZONE:
			return 1;

		default:
			return 0;
	}
}

local void TargetToSet(const Target *target, LinkedList *set)
{
	Link *link;
	Player *p;
	if (target->type == T_LIST)
	{
		for (link = LLGetHead(&target->u.list); link; link = link->next)
			LLAdd(set, link->data);
	}
	else if (target->type == T_PLAYER)
	{
		LLAdd(set, target->u.p);
	}
	else
	{
		RDLOCK();
		FOR_EACH_PLAYER(p)
			if (p->status == S_PLAYING && matches(target, p))
				LLAdd(set, p);
		RULOCK();
	}
}


/* per-player data stuff */

local LinkedList blocks;
struct block
{
	int start, len;
};

local int AllocatePlayerData(size_t bytes)
{
	Player *p;
	void *data;
	Link *link, *last = NULL;
	struct block *b, *nb;
	int current = 0;

	/* round up to next multiple of word size */
	bytes = (bytes+(sizeof(int)-1)) & (~(sizeof(int)-1));

	WRLOCK();

	/* first try before between two blocks (or at the beginning) */
	for (link = LLGetHead(&blocks); link; link = link->next)
	{
		b = link->data;
		if ((b->start - current) >= (int)bytes)
			goto found;
		else
			current = b->start + b->len;
		last = link;
	}

	/* if we couldn't get in between two blocks, try at the end */
	if ((perplayerspace - current) >= (int)bytes)
		goto found;

	/* no more space. if we're using dynamic player data space, we can
	 * grow it. if not, we're out of luck. */
#ifdef CFG_DYNAMIC_PPD
	/* round up to the next multiple of 1k */
	pthread_mutex_lock(&extradatamtx);
	perplayerspace += (bytes + 1023) & ~1023;
	FOR_EACH_PLAYER(p)
		p->playerextradata = arealloc(p->playerextradata, perplayerspace);
	pthread_mutex_unlock(&extradatamtx);
#else
	WULOCK();
	return -1;
#endif

found:
	nb = amalloc(sizeof(*nb));
	nb->start = current;
	nb->len = bytes;
	/* if last == NULL, this will put it in front of the list */
	LLInsertAfter(&blocks, last, nb);

	/* clear all newly allocated space */
	FOR_EACH_PLAYER_P(p, data, current)
		memset(data, 0, bytes);

	WULOCK();

	return current;
}

local void FreePlayerData(int key)
{
	Link *l;
	WRLOCK();
	for (l = LLGetHead(&blocks); l; l = l->next)
	{
		struct block *b = l->data;
		if (b->start == key)
		{
			LLRemove(&blocks, b);
			afree(b);
			break;
		}
	}
	WULOCK();
}


local void * GetPD(Player *p, int key)
{
	void *ret;
	assert(key > 0);
	pthread_mutex_lock(&extradatamtx);
	ret = (void*)(p->playerextradata + key);
	pthread_mutex_unlock(&extradatamtx);
	return ret;
}


/* interface */
local Iplayerdata pdint =
{
	INTERFACE_HEAD_INIT(I_PLAYERDATA, "playerdata")
	NewPlayer, FreePlayer, KickPlayer,
	LockPlayer, UnlockPlayer,
	PidToPlayer, FindPlayer,
	TargetToSet,
	AllocatePlayerData, FreePlayerData,
#ifdef CFG_DYNAMIC_PPD
	GetPD,
#endif
	Lock, WriteLock, Unlock, WriteUnlock
};


EXPORT int MM_playerdata(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		int i;
#ifndef CFG_DYNAMIC_PPD
		Iconfig *cfg;
#endif

		mm = mm_;

		/* init locks */
		pthread_mutexattr_init(&recmtxattr);
		pthread_mutexattr_settype(&recmtxattr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutexattr_settype(&recmtxattr, PTHREAD_MUTEX_RECURSIVE);
#ifdef USE_RWLOCK
		rwl_init(&plock);
#else
		pthread_mutex_init(&plock, &recmtxattr);
#endif

		/* init some basic data */
		pidmapsize = 256;
		pidmap = amalloc(pidmapsize * sizeof(Player*));
		for (i = 0; i < pidmapsize; i++)
			pidmap[i] = NULL;

		LLInit(&pd->playerlist);

		LLInit(&blocks);

#ifdef CFG_DYNAMIC_PPD
		pthread_mutex_init(&extradatamtx, NULL);
		perplayerspace = 1024; /* start small */
#else
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		perplayerspace = cfg ? cfg->GetInt(GLOBAL, "General", "PerPlayerBytes", 4000) : 4000;
		mm->ReleaseInterface(cfg);
#endif

		dummykey = AllocatePlayerData(4);
		magickey = AllocatePlayerData(sizeof(unsigned));
		mtxkey = AllocatePlayerData(sizeof(pthread_mutex_t));

		/* register interface */
		mm->RegInterface(&pdint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&pdint, ALLARENAS))
			return MM_FAIL;

		pthread_mutexattr_destroy(&recmtxattr);

		FreePlayerData(dummykey);
		FreePlayerData(magickey);
		FreePlayerData(mtxkey);

		afree(pidmap);
		LLEnum(&pd->playerlist, afree);
		LLEmpty(&pd->playerlist);
		return MM_OK;
	}
	return MM_FAIL;
}

