
/* dist: public */

#include <string.h>
#include <assert.h>

#include "rwlock.h"

#include "asss.h"

#define USE_RWLOCK

/* static data */

local Imodman *mm;
local int magickey, mtxkey;
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

local Player **pidmap;
local int pidmapsize;
local int perplayerspace;
local pthread_mutexattr_t recmtxattr;

/* forward declaration */
local Iplayerdata myint;


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
	Player *p = amalloc(sizeof(*p) + perplayerspace);

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
	LLAdd(&myint.playerlist, p);
	WULOCK();

	/* set up player struct and packet */
	p->pkt.pktype = S2C_PLAYERENTERING;
	p->pkt.pid = pid;
	p->status = S_CONNECTED;
	p->type = type;
	p->arena = NULL;
	p->oldarena = NULL;
	p->pid = pid;
	p->p_ship = SPEC;
	p->p_attached = -1;
	p->connecttime = current_ticks();
	p->connectas = NULL;

	return p;
}


local void FreePlayer(Player *p)
{
	WRLOCK();
	LLRemove(&myint.playerlist, p);
	pidmap[p->pid] = NULL;
	WULOCK();
	
	pthread_mutex_destroy((pthread_mutex_t*)PPDATA(p, mtxkey));

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
	Link *l;

	RDLOCK();
	for (l = LLGetHead(&myint.playerlist); l; l = l->next)
	{
		Player *p = l->data;
		if (strcasecmp(name, p->name) == 0)
		{
			RULOCK();
			return p;
		}
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
	if (target->type == T_LIST)
	{
		Link *l;
		for (l = LLGetHead(&target->u.list); l; l = l->next)
			LLAdd(set, l->data);
	}
	else if (target->type == T_PLAYER)
	{
		LLAdd(set, target->u.p);
	}
	else
	{
		Link *l;
		RDLOCK();
		for (l = LLGetHead(&myint.playerlist); l; l = l->next)
		{
			Player *p = l->data;
			if (p->status == S_PLAYING && matches(target, p))
				LLAdd(set, p);
		}
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
	Link *l, *last = NULL;
	struct block *b, *nb;
	int current = 0;

	/* round up to next multiple of word size */
	bytes = (bytes+(sizeof(int)-1)) & (~(sizeof(int)-1));

	WRLOCK();
	/* first try before between two blocks (or at the beginning) */
	for (l = LLGetHead(&blocks); l; l = l->next)
	{
		b = l->data;
		if ((b->start - current) >= (int)bytes)
		{
			nb = amalloc(sizeof(*nb));
			nb->start = current;
			nb->len = bytes;
			/* if last == NULL, this will put it in front of the list */
			LLInsertAfter(&blocks, last, nb);
			WULOCK();
			return current;
		}
		else
			current = b->start + b->len;
		last = l;
	}

	/* if we couldn't get in between two blocks, try at the end */
	if ((perplayerspace - current) >= (int)bytes)
	{
		nb = amalloc(sizeof(*nb));
		nb->start = current;
		nb->len = bytes;
		LLInsertAfter(&blocks, last, nb);
		WULOCK();
		return current;
	}

	WULOCK();
	return -1;
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


/* interface */
local Iplayerdata myint =
{
	INTERFACE_HEAD_INIT(I_PLAYERDATA, "playerdata")
	NewPlayer, FreePlayer, KickPlayer,
	LockPlayer, UnlockPlayer,
	PidToPlayer, FindPlayer,
	TargetToSet,
	AllocatePlayerData, FreePlayerData,
	Lock, WriteLock, Unlock, WriteUnlock
};


EXPORT int MM_playerdata(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		int i;
		Iconfig *cfg;

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

		LLInit(&myint.playerlist);

		LLInit(&blocks);

		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		perplayerspace = cfg ? cfg->GetInt(GLOBAL, "General", "PerPlayerBytes", 4000) : 4000;
		mm->ReleaseInterface(cfg);

		magickey = AllocatePlayerData(sizeof(unsigned));
		mtxkey = AllocatePlayerData(sizeof(pthread_mutex_t));

		/* register interface */
		mm->RegInterface(&myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myint, ALLARENAS))
			return MM_FAIL;

		pthread_mutexattr_destroy(&recmtxattr);

		afree(pidmap);
		LLEnum(&myint.playerlist, afree);
		LLEmpty(&myint.playerlist);
		return MM_OK;
	}
	return MM_FAIL;
}

