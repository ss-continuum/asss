
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef WIN32
#include <sys/time.h>
#endif

/* make sure to get the prototypes for thread functions instead of macros */
#define USE_PROTOTYPES

#include "util.h"
#include "defs.h"


/* NOTE about util and memory allocation
 *
 * Since I want this file to be used in both the main server and the
 * scheme server, which uses garbage collection, it has to be a bit
 * flexible with regard to memory management. To use it normally just
 * compile it normally. It will use the system malloc. To use it with a
 * garbage collector, #define USE_GC, and #define MALLOC to whatever you
 * want to use to allocate pointerful memory.
 */


#ifdef MALLOC
/* prototype it */
void *MALLOC(size_t);
#else
/* default */
#define MALLOC(s) malloc(s)
#endif


#define DEFTABLESIZE 29



typedef struct HashEntry
{
	void *p;
	struct HashEntry *next;
	char *key; /* points into the table's stringchunk */
} HashEntry;

struct HashTable
{
	int size;
	StringChunk *sc;
	HashEntry *lists[0];
};



#ifndef USE_GC

static Link *freelinks = NULL;
static HashEntry *freehashentries = NULL;

#endif


unsigned int GTC(void)
{
#ifndef WIN32
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return (tv.tv_sec * 100 + tv.tv_usec / 10000) & 0x7FFFFFFF;
#else
	return GetTickCount() / 10;
#endif
}


char *RemoveCRLF(char *p)
{
	char *t;
	if ((t = strchr(p,0x0A))) *t = 0;
	if ((t = strchr(p,0x0D))) *t = 0;
	return p;
}

char *ToLowerStr(char *str)
{
	char *orig = str;
	while (*str)
	{
		*str = tolower(*str);
		str++;
	}
	return orig;
}

void *amalloc(size_t s)
{
	void *ptr;
	ptr = MALLOC(s);
	if (!ptr)
		Error(ERROR_MEMORY,"malloc error: requested %i bytes\n",s);
#ifndef USE_GC
	memset(ptr, 0, s);
#endif
	return ptr;
}

char *astrdup(const char *s)
{
	char *r = strdup(s);
	if (!r)
		Error(ERROR_MEMORY,"strdup error\n");
	return r;
}

void afree(void *ptr)
{
#ifndef USE_GC
	free(ptr);
#endif
}

void Error(int level, char *format, ...)
{
	va_list argptr;
	fprintf(stderr,"Unrecoverable error (%i): ", level);
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
	fprintf(stderr, "\n");
	exit(level);
}


char *astrncpy(char *dest, const char *source, size_t n)
{
	strncpy(dest, source, n-1);
	dest[n-1] = 0;
	return dest;
}


/* LinkedList data type */

#ifndef USE_GC

#define LINKSATONCE 510 /* enough to almost fill a page */

local void GetSomeLinks(void)
{
	Link *mem, *start;
	int i;

	start = mem = amalloc(LINKSATONCE * sizeof(Link));
	for (i = 0; i < LINKSATONCE-1; i++, mem++)
		mem->next = mem + 1;
	mem->next = freelinks;
	freelinks = start;
}

#endif

void LLInit(LinkedList *lst)
{
	lst->start = lst->end = NULL;
}

LinkedList * LLAlloc(void)
{
	LinkedList *ret;
#ifdef USE_GC
	ret = amalloc(sizeof(Link));
#else
	/* HACK: depends on LinkedList and Link being the same size */
	if (!freelinks) GetSomeLinks();
	ret = (LinkedList*) freelinks;
	freelinks = freelinks->next;
#endif
	LLInit(ret);
	return ret;
}

void LLEmpty(LinkedList *l)
{
#ifndef USE_GC
	Link *n = l->start, *t;

	if (n)
	{
		t = freelinks;
		freelinks = n;
		while (n->next)
			n = n->next;
		n->next = t;
	}
#endif
	l->start = l->end = NULL;
}

void LLFree(LinkedList *lst)
{
	/* HACK: see above */
	LLEmpty(lst);
#ifndef USE_GC
	((Link*)lst)->next = freelinks;
	freelinks = (Link*)lst;
#endif
}

void LLAdd(LinkedList *l, void *p)
{
	Link *n;

#ifdef USE_GC
	n = amalloc(sizeof(Link));
#else
	if (!freelinks) GetSomeLinks();
	n = freelinks;
	freelinks = freelinks->next;
#endif

	n->next = NULL;
	n->data = p;

	if (l->end)
	{
		l->end->next = n;
		l->end = n;
	}
	else
	{
		l->start = l->end = n;
	}
}

void LLAddFirst(LinkedList *lst, void *data)
{
	Link *n;
	
#ifdef USE_GC
	n = amalloc(sizeof(Link));
#else
	if (!freelinks) GetSomeLinks();
	n = freelinks;
	freelinks = freelinks->next;
#endif

	n->next = lst->start;
	n->data = data;

	lst->start = n;
	if (lst->end == NULL)
		lst->end = n;
}

int LLRemove(LinkedList *l, void *p)
{
	Link *n = l->start, *prev = NULL;
	while (n)
	{
		if (n->data == p)
		{
			if (l->start == n)
			{
				l->start = n->next;
				if (l->start == NULL) l->end = NULL;
			}
			else
			{
				prev->next = n->next;
				if (n == l->end) l->end = prev;
			}
#ifdef USE_GC
			n->next = n->data = NULL;
			n = NULL;
#else
			n->next = freelinks;
			freelinks = n;
#endif
			return 1;
		}
		prev = n;
		n = n->next;
	}
	return 0;
}

int LLRemoveAll(LinkedList *l, void *p)
{
	Link *n = l->start, *prev = NULL, *next;
	int removed = 0;

	while (n)
	{
		next = n->next;
		if (n->data == p)
		{
			if (prev == NULL) /* first link */
			{
				l->start = next;
			}
			else
			{
				prev->next = next;
				if (next == NULL) l->end = prev;
			}
#ifdef USE_GC
			n->next = n->data = NULL;
			n = NULL;
#else
			n->next = freelinks;
			freelinks = n;
#endif
			removed++;
		}
		else
			prev = n;
		n = next;
	}

	return removed;
}

void *LLRemoveFirst(LinkedList *lst)
{
	Link *lnk;
	void *ret;

	if (lst->start == NULL)
		return NULL;

	ret = lst->start->data;

	lnk = lst->start;
	lst->start = lst->start->next;

#ifdef USE_GC
	lnk->next = lnk->data = NULL;
	lnk = NULL;
#else
	lnk->next = freelinks;
	freelinks = lnk;
#endif

	if (lst->start == NULL)
		lst->end = NULL;

	return ret;
}

Link * LLGetHead(LinkedList *l)
{
	return l->start;
}

int LLIsEmpty(LinkedList *lst)
{
	return lst->start == NULL;
}

/* HashTable data type */

/* note: this is a case-insensitive hash! */
inline unsigned Hash(const char *s, int modulus)
{
	unsigned len = 3, ret = 1447;
	while (*s)
		ret = (ret * tolower(*s++) + len++ *7) % modulus;
	return ret % modulus;
}

HashTable * HashAlloc(int req)
{
	int size = req ? req : DEFTABLESIZE;
	HashTable *h = amalloc(sizeof(HashTable) + size * sizeof(HashEntry*));
	h->sc = SCAlloc();
	h->size = size;
	return h;
}

void HashFree(HashTable *h)
{
#ifndef USE_GC
	HashEntry *e, *old;
	int i;
	for (i = 0; i < h->size; i++)
	{
		e = h->lists[i];
		if (e)
		{
			old = freehashentries;
			freehashentries = e;
			while (e->next)
				e = e->next;
			e->next = old;
		}
	}
	SCFree(h->sc);
	afree(h);
#endif
}

void HashEnum(HashTable *h, void (*func)(char *key, void *val, void *data), void *data)
{
	HashEntry *e;
	int i;
	for (i = 0; i < h->size; i++)
	{
		e = h->lists[i];
		while (e)
		{
			func(e->key, e->p, data);
			e = e->next;
		}
	}
}

void HashAdd(HashTable *h, const char *s, void *p)
{
	int slot;
	HashEntry *e, *l;

#ifndef USE_GC
	if (freehashentries)
	{
		e = freehashentries;
		freehashentries = e->next;
	}
	else
#endif
		e = amalloc(sizeof(HashEntry));

	slot = Hash(s, h->size);

	/* look through the bucket for matching keys to see if we can steal
	 * their stored key. */
	l = h->lists[slot];
	e->key = NULL;
	while (l)
	{
		if (!strcasecmp(l->key, s))
		{
			e->key = l->key;
			break;
		}
		l = l->next;
	}

	/* if we couldn't find one, make new key */
	if (e->key == NULL)
		e->key = SCAdd(h->sc, s);

	e->p = p;
	e->next = NULL;

	l = h->lists[slot];
	if (!l)
	{	/* this is first hash entry for this key */
		h->lists[slot] = e;
	}
	else
	{	/* find end of list and insert it */
		while (l->next) l = l->next;
		l->next = e;
	}
}

void HashReplace(HashTable *h, const char *s, void *p)
{
	int slot;
	HashEntry *e, *l;

	slot = Hash(s, h->size);
	l = h->lists[slot];

	if (!l)
	{	/* this is first hash entry for this key */

		/* allocate entry */
#ifndef USE_GC
		if (freehashentries)
		{
			e = freehashentries;
			freehashentries = e->next;
		}
		else
#endif
			e = amalloc(sizeof(HashEntry));

		/* init entry */
		e->key = SCAdd(h->sc, s);
		e->p = p;
		e->next = NULL;

		/* install entry */
		h->lists[slot] = e;
	}
	else
	{	/* try to find it */
		HashEntry *last;
		do {
			if (!strcasecmp(s, l->key))
			{	/* found it, replace data and return */
				l->p = p;
				return;
			}
			last = l;
			l = l->next;
		} while (l);
		/* it's not in the table, last should point to last entry */
		
		/* allocate entry */
#ifndef USE_GC
		if (freehashentries)
		{
			e = freehashentries;
			freehashentries = e->next;
		}
		else
#endif
			e = amalloc(sizeof(HashEntry));

		/* init entry */
		e->key = SCAdd(h->sc, s);
		e->p = p;
		e->next = NULL;

		/* install entry */
		last->next = e;
	}
}

void HashRemove(HashTable *h, const char *s, void *p)
{
	int slot;
	HashEntry *l, *prev = NULL;

	slot = Hash(s, h->size);
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key) && l->p == p)
		{
			if (prev)
				prev->next = l->next;
			else /* removing first item */
				h->lists[slot] = l->next;
#ifdef USE_GC
			l = NULL;
#else
			l->next = freehashentries;
			freehashentries = l;
#endif
			return;
		}
		prev = l;
		l = l->next;
	}
}

LinkedList * HashGet(HashTable *h, const char *s)
{
	LinkedList *res = LLAlloc();
	HashGetAppend(h, s, res);
	return res;
}

void HashGetAppend(HashTable *h, const char *s, LinkedList *res)
{
	int slot;
	HashEntry *l;

	slot = Hash(s, h->size);
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
			LLAdd(res, l->p);
		l = l->next;
	}
}

void *HashGetOne(HashTable *h, const char *s)
{
	int slot;
	HashEntry *l;

	slot = Hash(s, h->size);
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
			return l->p;
		l = l->next;
	}
	return NULL;
}


#ifndef NODQ

void DQInit(DQNode *node)
{
	node->prev = node->next = node;
}

void DQAdd(DQNode *base, DQNode *node)
{
	base->prev->next = node;
	node->prev = base->prev;
	base->prev = node;
	node->next = base;
}

void DQRemove(DQNode *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = node->prev = node;
}

#endif


#ifndef NOSTRINGCHUNK

#define SCSIZE 4000

struct StringChunk
{
	struct StringChunk *next;
	int room;
	char data[SCSIZE];
};

StringChunk *SCAlloc(void)
{
	StringChunk *c;

	c = amalloc(sizeof(StringChunk));
	c->next = NULL;
	c->room = SCSIZE;
	return c;
}

char *SCAdd(StringChunk *chunk, const char *str)
{
	int len;
	StringChunk *prev = NULL;

	len = strlen(str)+1;

	if (len > SCSIZE)
	{
		/* too big for normal chunk. get specially sized chunk. */
		while (chunk)
		{
			prev = chunk;
			chunk = chunk->next;
		}
		if (prev)
		{
			StringChunk *new = amalloc(sizeof(StringChunk) - SCSIZE + len);
			new->next = NULL;
			new->room = 0;
			memcpy(new->data, str, len);
			prev->next = new;
			return new->data;
		}
		else
			return NULL;
	}

	while (chunk)
	{
		if (chunk->room >= len)
		{
			char *spot;			
			spot = chunk->data + (SCSIZE - chunk->room);
			memcpy(spot, str, len);
			chunk->room -= len;
			return spot;
		}
		prev = chunk;
		chunk = chunk->next;
	}

	/* no room in any present chunk */
	if (prev)
	{
		chunk = SCAlloc();
		prev->next = chunk;
		/* recursive call to add it */
		return SCAdd(chunk, str);
	}
	else /* got a null chunk */
		return NULL;
}

void SCFree(StringChunk *chunk)
{
	StringChunk *old;
	while (chunk)
	{
		old = chunk->next;
		afree(chunk);
		chunk = old;
	}
}

#endif


#ifndef NOTHREAD

#include "pthread.h"

#ifndef NOMPQUEUE

void MPInit(MPQueue *q)
{
	LLInit(&q->list);
	InitMutex(&q->mtx);
	InitCondition(&q->cond);
}

void MPDestroy(MPQueue *q)
{
	LLEmpty(&q->list);
/*	DestroyMutex(&q->mtx);
	DestroyCondition(&q->cond); */
}

void MPAdd(MPQueue *q, void *data)
{
	LockMutex(&q->mtx);
	LLAdd(&q->list, data);
	SignalCondition(&q->cond, 0);
	UnlockMutex(&q->mtx);
}

void * MPTryRemove(MPQueue *q)
{
	void *data;
	LockMutex(&q->mtx);
	data = LLRemoveFirst(&q->list);
	UnlockMutex(&q->mtx);
	return data;
}
	
void * MPRemove(MPQueue *q)
{
	void *data;
	LockMutex(&q->mtx);
	while (LLIsEmpty(&q->list))
		WaitCondition(&q->cond, &q->mtx);
	data = LLRemoveFirst(&q->list);
	UnlockMutex(&q->mtx);
	return data;
}

#endif /* MPQUEUE */


Thread StartThread(ThreadFunc func, void *data)
{
	Thread ret;
	if (pthread_create(&ret, NULL, func, data) == 0)
		return ret;
	else
		return 0;
}

void JoinThread(Thread thd)
{
	void *dummy;
	pthread_join(thd, &dummy);
}

void InitMutex(Mutex *mtx)
{
	pthread_mutex_init(mtx, NULL);
}
	
void LockMutex(Mutex *mtx)
{
	pthread_mutex_lock(mtx);
}

void UnlockMutex(Mutex *mtx)
{
	pthread_mutex_unlock(mtx);
}

void InitCondition(Condition *cond)
{
	pthread_cond_init(cond, NULL);
}

void SignalCondition(Condition *cond, int all)
{
	if (all)
		pthread_cond_broadcast(cond);
	else
		pthread_cond_signal(cond);
}

void WaitCondition(Condition *cond, Mutex *mtx)
{
	pthread_cond_wait(cond, mtx);
}

void WaitConditionTimed(Condition *cond, Mutex *mtx, int millis)
{
#ifndef WIN32
	struct timeval tv;
	struct timespec ts;

	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = (tv.tv_usec + millis * 1000) * 1000;
	while (ts.tv_nsec >= 1000000000)
	{
		ts.tv_nsec -= 1000000000;
		ts.tv_sec++;
	}
	pthread_cond_timedwait(cond, mtx, &ts);
#else
	/* GetTickCount() isn't enough here: pthread_cond_timedwait requires
	 * an absolute time relative to the epoch. either way, this function
	 * isn't currently used (and probably shouldn't ever be). */
	Error(ERROR_GENERAL, "WaitConditionTimed isn't implemented on windows");
#endif
}

#endif /* THREAD */


