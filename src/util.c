
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef NOTHREAD
#include <pthread.h>
#endif


#include "util.h"
#include "defs.h"


#define DEFTABLESIZE 229
#define MAXHASHLEN 63




typedef struct HashEntry
{
	void *p;
	struct HashEntry *next;
	char key[MAXHASHLEN+1];
} HashEntry;

struct HashTable
{
	int size;
	HashEntry *lists[0];
};




static Link *freelinks = NULL;
static HashEntry *freehashentries = NULL;




unsigned int GTC()
{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec * 100 + tv.tv_usec / 10000;
}


char* RemoveCRLF(char *p)
{
	char *t;
	if ((t = strchr(p,0x0A))) *t = 0;
	if ((t = strchr(p,0x0D))) *t = 0;
	return p;
}


void *amalloc(size_t s)
{
	void *ptr = malloc(s);
	if (!ptr)
		Error(ERROR_MEMORY,"malloc error: requested %i bytes\n",s);
	memset(ptr, 0, s);
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
	free(ptr);
}


void Error(int level, char *format, ...)
{
	va_list argptr;
	fprintf(stderr,"Unrecoverable error (%i): ", level);
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
	exit(level);
}


char *astrncpy(char *dest, const char *source, size_t n)
{
	strncpy(dest, source, n-1);
	dest[n] = 0;
	return dest;
}


/* LinkedList data type */

#define LINKSATONCE 510 /* enough to almost fill a page */

local void GetSomeLinks()
{
	Link *mem, *start;
	int i;

	start = mem = amalloc(LINKSATONCE * sizeof(Link));
	for (i = 0; i < LINKSATONCE-1; i++, mem++)
		mem->next = mem + 1;
	mem->next = freelinks;
	freelinks = start;
}

void LLInit(LinkedList *lst)
{
	lst->start = lst->end = NULL;
}

LinkedList * LLAlloc()
{
	LinkedList *ret;
	/* HUGE HACK!!!
	 * depends on LinkedList and Link being the same size!
	 */
	if (!freelinks) GetSomeLinks();
	ret = (LinkedList*) freelinks;
	freelinks = freelinks->next;
	LLInit(ret);
	return ret;
}

local void LLEmpty(LinkedList *l)
{
	Link *n = l->start, *t;

	if (n)
	{
		t = freelinks;
		freelinks = n;
		while (n->next)
			n = n->next;
		n->next = t;
	}
	l->start = l->end = NULL;
}

void LLFree(LinkedList *lst)
{
	/* HUGE HACK!!!
	 * see above
	 */
	Link *l;
	LLEmpty(lst);
	l = (Link*) lst;
	l->next = freelinks;
	freelinks = l;
}

void LLAdd(LinkedList *l, void *p)
{
	Link *n;

	if (!freelinks) GetSomeLinks();
	n = freelinks;
	freelinks = freelinks->next;

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
			n->next = freelinks;
			freelinks = n;
			return 1;
		}
		prev = n;
		n = n->next;
	}
	return 0;
}

Link * LLGetHead(LinkedList *l)
{
	return l->start;
}


/* HashTable data type */

inline unsigned Hash(const char *s, int maxlen, int modulus)
{
	unsigned len = 0, ret = 1447;
	while (*s && len++ < maxlen)
		ret = (ret * tolower(*s++) + len*7) % modulus;
	return ret;
}

HashTable * HashAlloc(int req)
{
	int size = req ? req : DEFTABLESIZE;
	HashTable *h = amalloc(sizeof(HashTable) + size * sizeof(HashEntry));
	h->size = size;
	return h;
}

void HashFree(HashTable *h)
{
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
	afree(h);
}

void HashEnum(HashTable *h, void (*func)(void *))
{
	HashEntry *e;
	int i;
	for (i = 0; i < h->size; i++)
	{
		e = h->lists[i];
		while (e)
		{
			func(e->p);
			e = e->next;
		}
	}
}

void HashAdd(HashTable *h, const char *s, void *p)
{
	int slot;
	HashEntry *e, *l;

	if (freehashentries)
	{
		e = freehashentries;
		freehashentries = e->next;
	}
	else
		e = amalloc(sizeof(HashEntry));

	slot = Hash(s, MAXHASHLEN, h->size);
	l = h->lists[slot];

	astrncpy(e->key, s, MAXHASHLEN+1);
	e->p = p;
	e->next = NULL;

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

void HashRemove(HashTable *h, const char *s, void *p)
{
	int slot;
	HashEntry *l, *prev = NULL;

	slot = Hash(s, MAXHASHLEN, h->size);
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key) && l->p == p)
		{
			if (prev)
				prev->next = l->next;
			else /* removing first item */
				h->lists[slot] = l->next;
			l->next = freehashentries;
			freehashentries = l;
		}
		prev = l;
		l = l->next;
	}
}

LinkedList * HashGet(HashTable *h, const char *s)
{
	int slot;
	HashEntry *l;
	LinkedList *res;

	res = LLAlloc();

	slot = Hash(s, MAXHASHLEN, h->size);
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
			LLAdd(res, l->p);
		l = l->next;
	}
	return res;
}


/*  int hashfunction(s) char *s; { int i; */
/*  for( i=0; *s; s++ ) i = 131*i */
/*  + *s; return( i % m ); */


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
	UnlockMutex(&q->mtx);
	SignalCondition(&q->cond, 0);
}

	
void * MPRemove(MPQueue *q)
{
	void *data;
	Link *l;

	LockMutex(&q->mtx);
	while ( !(l = LLGetHead(&q->list)) )
		WaitCondition(&q->cond, &q->mtx);
	data = l->data;
	LLRemove(&q->list, data);
	UnlockMutex(&q->mtx);
	return data;
}

#endif /* MPQUEUE */


#ifndef NOTHREAD

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


#endif /* THREAD */


