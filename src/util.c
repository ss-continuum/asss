
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>


#include "asss.h"


#define DEFTABLESIZE 229
#define MAXHASHLEN 63


typedef struct Link
{
	struct Link *next;
	void *p;
} Link;

struct LinkedList
{
	Link *start, *end, *current;
};

typedef struct HashEntry
{
	void *p;
	struct HashEntry *next;
	char key[MAXHASHLEN+1];
} HashEntry;

struct HashTable
{
	LinkedList *result;
	int size;
	HashEntry *lists[0];
};





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
	return amalloc(sizeof(LinkedList));
}

local void LLEmpty(LinkedList *l)
{
	Link *n = l->start, *t;
	while (n)
	{
		t = n;
		n = n->next;
		free(t);
	}
	l->start = l->current = l->end = NULL;
}

void LLFree(LinkedList *l)
{
	LLEmpty(l);
	free(l);
}

void LLAdd(LinkedList *l, void *p)
{
	Link *n = amalloc(sizeof(Link));
	n->next = NULL;
	n->p = p;

	if (l->end)
	{
		l->end->next = n;
		l->end = n;
	}
	else
	{
		l->start = l->end = l->current = n;
	}
}

int LLRemove(LinkedList *l, void *p)
{
	Link *n = l->start, *prev = NULL;
	while (n)
	{
		if (n->p == p)
		{
			if (l->current == n)
				l->current = n->next;
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
			free(n);
			return 1;
		}
		prev = n;
		n = n->next;
	}
	return 0;
}

void LLRewind(LinkedList *l)
{
	l->current = l->start;
}

void * LLNext(LinkedList *l)
{
	void *t;
	if (!l->current) return NULL;
	t = l->current->p;
	l->current = l->current->next;
	return t;
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
	h->result = LLAlloc();
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
		while (e)
		{
			old = e;
			e = e->next;
			free(old);
		}
	}
	LLFree(h->result);
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
	HashEntry *e = amalloc(sizeof(HashEntry)), *l;

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
			free(l);
		}
		prev = l;
		l = l->next;
	}
}

LinkedList * HashGet(HashTable *h, const char *s)
{
	int slot;
	HashEntry *l;
	LinkedList *res = h->result;

	slot = Hash(s, MAXHASHLEN, h->size);
	l = h->lists[slot];

	LLEmpty(res);
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




