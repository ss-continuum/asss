
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef WIN32
#include <sys/time.h>
#else
/* for GetTickCount() */
#include <windows.h>
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
	char *s = str;
	for (s = str; *s; s++)
		if (isupper(*s))
			*s = tolower(*s);
	return str;
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
	char *r;
	if (!s)
		return NULL;
	r = strdup(s);
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


int strsplit(const char *big, const char *delims, char *buf, int buflen, const char **ptmp)
{
	const char *tmp = *ptmp;
	/* if the first time, init pointer to start of string */
	if (!tmp)
	{
		if (big)
			tmp = big;
		else
			return 0;
	}
	/* now pick up where we left off */
	/* move past delims */
	while (*tmp && strchr(delims, *tmp)) tmp++;
	/* check if we moved off end of string */
	if (!*tmp) return 0;
	/* copy into buf until max or delim or end of string */
	for ( ; *tmp && !strchr(delims, *tmp); tmp++)
		if (buflen > 1)
		{
			*buf++ = *tmp;
			buflen--;
		}
	/* terminate with nil */
	*buf = '\0';
	/* replace tmp pointer */
	*ptmp = tmp;
	return 1;
}


/* LinkedList data type */

#define LINKSATONCE 510 /* enough to almost fill a page */

#ifdef _REENTRANT

#include <pthread.h>

local pthread_mutex_t freelinkmtx = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_FREE() pthread_mutex_lock(&freelinkmtx)
#define UNLOCK_FREE() pthread_mutex_unlock(&freelinkmtx)

#else

#define LOCK_FREE()
#define UNLOCK_FREE()

#endif

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

local Link *GetALink(void)
{
	Link *ret;
	LOCK_FREE();
	if (!freelinks) GetSomeLinks();
	ret = freelinks;
	freelinks = freelinks->next;
	UNLOCK_FREE();
	return ret;
}

local void FreeALink(Link *l)
{
	LOCK_FREE();
	l->next = freelinks;
	freelinks = l;
	UNLOCK_FREE();
}


void LLInit(LinkedList *lst)
{
	lst->start = lst->end = NULL;
}

LinkedList * LLAlloc(void)
{
	LinkedList *ret;
	/* HACK: depends on LinkedList and Link being the same size */
	ret = (LinkedList*)GetALink();
	LLInit(ret);
	return ret;
}

void LLEmpty(LinkedList *l)
{
	Link *n = l->start, *t;

	if (n)
	{
		LOCK_FREE();
		t = freelinks;
		freelinks = n;
		while (n->next)
			n = n->next;
		n->next = t;
		UNLOCK_FREE();
	}
	l->start = l->end = NULL;
}

void LLFree(LinkedList *lst)
{
	/* HACK: see above */
	LLEmpty(lst);
	FreeALink((Link*)lst);
}

void LLAdd(LinkedList *l, void *p)
{
	Link *n;

	n = GetALink();

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

	n = GetALink();

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
			FreeALink(n);
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
			FreeALink(n);
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

	FreeALink(lnk);

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

int LLCount(LinkedList *ll)
{
	Link *l;
	int c = 0;
	for (l = ll->start; l; l = l->next) c++;
	return c;
}

void LLEnum(LinkedList *lst, void (*func)(void *ptr))
{
	Link *l;
	for (l = lst->start; l; l = l->next)
		func(l->data);
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

int DQCount(DQNode *node)
{
	DQNode *n;
	int c = 0;
	for (n = node->next; n != node; n = n->next)
		c++;
	return c;
}

#endif



#ifndef NOTREAP

local TreapHead **tr_find(TreapHead **root, int key)
{
	for (;;)
		if ((*root) == NULL)
			return NULL;
		else if ((*root)->key == key)
			return root;
		else if ((*root)->key < key)
			root = &(*root)->right;
		else
			root = &(*root)->left;
}

TreapHead *TrGet(TreapHead *root, int key)
{
	TreapHead **p = tr_find(&root, key);
	return p ? *p : NULL;
}

#define TR_ROT_LEFT(node)              \
do {                                   \
    TreapHead *tmp = (*node)->right;   \
    (*node)->right = tmp->left;        \
    tmp->left = *node;                 \
    *node = tmp;                       \
} while(0)

#define TR_ROT_RIGHT(node)             \
do {                                   \
    TreapHead *tmp = (*node)->left;    \
    (*node)->left = tmp->right;        \
    tmp->right = *node;                \
    *node = tmp;                       \
} while(0)                             \

void TrPut(TreapHead **root, TreapHead *node)
{
	if (*root == NULL)
	{
		node->pri = rand();
		node->left = node->right = NULL;
		*root = node;
	}
	else if ((*root)->key < node->key)
	{
		TrPut(&(*root)->right, node);
		/* the node might now be the right child of root */
		if ((*root)->pri > (*root)->right->pri)
			TR_ROT_LEFT(root);
	}
	else
	{
		TrPut(&(*root)->left, node);
		/* the node might now be the left child of root */
		if ((*root)->pri > (*root)->left->pri)
			TR_ROT_RIGHT(root);
	}
}

void TrDelKey(TreapHead **root, int key)
{
	TreapHead **node;

	node = tr_find(root, key);
	if (node == NULL)
		return;

	while ((*node)->left || (*node)->right)
		if ((*node)->left == NULL)
			TR_ROT_LEFT(node);
		else if ((*node)->right == NULL)
			TR_ROT_RIGHT(node);
		else if ((*node)->right->pri < (*node)->left->pri)
			TR_ROT_LEFT(node);
		else
			TR_ROT_RIGHT(node);

	*node = NULL;
}

void TrEnum(TreapHead *root, void *clos, void (*func)(TreapHead *node, void *clos))
{
	if (root)
	{
		TreapHead *t;
		TrEnum(root->left, clos, func);
		/* save right child now because func might free it */
		t = root->right;
		func(root, clos);
		TrEnum(t, clos, func);
	}
}

void tr_enum_afree(TreapHead *node, void *dummy)
{
	afree(node);
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


#ifndef NOMPQUEUE

#include "pthread.h"

void MPInit(MPQueue *q)
{
	LLInit(&q->list);
	pthread_mutex_init(&q->mtx, NULL);
	pthread_cond_init(&q->cond, NULL);
}

void MPDestroy(MPQueue *q)
{
	LLEmpty(&q->list);
	pthread_mutex_destroy(&q->mtx);
	pthread_cond_destroy(&q->cond);
}

void MPAdd(MPQueue *q, void *data)
{
	pthread_mutex_lock(&q->mtx);
	LLAdd(&q->list, data);
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->mtx);
}

void * MPTryRemove(MPQueue *q)
{
	void *data;
	pthread_mutex_lock(&q->mtx);
	data = LLRemoveFirst(&q->list);
	pthread_mutex_unlock(&q->mtx);
	return data;
}
	
void * MPRemove(MPQueue *q)
{
	void *data;
	pthread_mutex_lock(&q->mtx);
	while (LLIsEmpty(&q->list))
		pthread_cond_wait(&q->cond, &q->mtx);
	data = LLRemoveFirst(&q->list);
	pthread_mutex_unlock(&q->mtx);
	return data;
}

#endif /* MPQUEUE */


