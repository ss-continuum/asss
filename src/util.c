
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>


#include "util.h"

#include "defs.h"


#define DEFTABLESIZE 229
#define MAXHASHLEN 63




struct LinkedList
{
	Link *start, *end;
};

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

LinkedList * LLAlloc()
{
	/* HUGE HACK!!!
	 * depends on LinkedList and Link being the same size!
	 */
	if (freelinks)
	{
		LinkedList *ret = (LinkedList*) freelinks;
		freelinks = freelinks->next;
		ret->start = ret->end = NULL;
		return ret;
	}
	else
		return amalloc(sizeof(LinkedList));
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

	if (freelinks)
	{
		n = freelinks;
		freelinks = freelinks->next;
	}
	else
		n = amalloc(sizeof(Link));

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
	free(h);
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




