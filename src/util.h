
#ifndef __UTIL_H
#define __UTIL_H

/*
 * util
 *
 * these are various utility functions that you can (and should) use.
 *
 * GTC gets the current time, in hundredths of a second. use this for
 * timing stuff.
 *
 * RemoveCRLF strips CR's and LF's from strings. probably not too useful
 * except for file i/o (which you should let other modules handle).
 *
 * amalloc is how you should allocate memory. it will never return NULL.
 * same for astrdup. use free as normal to free memory from these.
 *
 * astrncpy should be used instead of strncpy. its interface makes
 * sense, as opposed to strncpy. the size is actually the length of the
 * destination buffer. at most length-1 characters will be copied and
 * the last byte will always be null.
 *
 * you can use Error to signal a really terrible horrible error
 * condition that should stop the server. this one will call abort().
 * don't use unnecessarily.
 *
 * LinkedList:
 * use LLAlloc to get a list, or allocate it statically and call LLInit.
 * LLAdd to add stuff to the end (in the form of void pointers), and
 * LLAddFirst to insert stuff in the beginning.  LLRemove to remove at
 * most one matching item (so you can have duplicates and remove them
 * one at a time). LLRemoveFirst to remove the first item and return it
 * directly.  LLEmpty to clear it, freeing all the links, and LLFree to
 * free it.
 *
 * HashTable:
 * HashAlloc to get one. HashAdd and HashRemove to add and remove stuff.
 * HashReplace to keep the table free of duplicates.  HashGet will
 * return a linked list with all the matching items. yes, you can store
 * duplicates in it and get them all out at once.  HashGetOne can be
 * used when you only want the first match.
 *
 */


/* include for size_t */
#include <stddef.h>


/* miscelaneous stuff */

unsigned int GTC(void);

char *RemoveCRLF(char *str);
char *ToLowerStr(char *str);

void *amalloc(size_t bytes);
char *astrdup(const char *str);
void afree(const void *ptr);

char *astrncpy(char *dest, const char *source, size_t destlength);
const char *delimcpy(char *dest, const char *source, size_t destlen, char delim);

int strsplit(const char *big, const char *delims, char *buf, int buflen, const char **ptmp);

void Error(int errorcode, char *message, ...);


/* list manipulation functions */

/* it's ok to access these fields directly */
typedef struct Link
{
	struct Link *next;
	void *data;
} Link;

/* DON'T access this directly, use LLGetHead */
struct LinkedList
{
	Link *start, *end;
};

typedef struct LinkedList LinkedList;

LinkedList * LLAlloc(void);
void LLInit(LinkedList *lst);
void LLEmpty(LinkedList *lst);
void LLFree(LinkedList *lst);
void LLAdd(LinkedList *lst, void *data);
void LLAddFirst(LinkedList *lst, void *data);
int LLRemove(LinkedList *lst, void *data);
int LLRemoveAll(LinkedList *lst, void *data);
void *LLRemoveFirst(LinkedList *lst);
int LLIsEmpty(LinkedList *lst);
int LLCount(LinkedList *lst);
void LLEnum(LinkedList *lst, void (*func)(const void *ptr));

#ifdef USE_PROTOTYPES
Link *LLGetHead(LinkedList *lst);
#else
#define LLGetHead(lst) ((lst)->start)
#define LLIsEmpty(lst) ((lst)->start == NULL)
#endif


/* hashing stuff */

typedef struct HashTable HashTable;

HashTable * HashAlloc(int);
void HashFree(HashTable *ht);
void HashEnum(HashTable *ht, void (*func)(char *key, void *val, void *data), void *data);
void HashAdd(HashTable *ht, const char *key, void *data);
void HashReplace(HashTable *ht, const char *key, void *data);
void HashRemove(HashTable *ht, const char *key, void *data);
LinkedList *HashGet(HashTable *ht, const char *key);
void HashGetAppend(HashTable *ht, const char *key, LinkedList *ll);
void *HashGetOne(HashTable *ht, const char *key);


#ifndef NODQ

typedef struct DQNode
{
	struct DQNode *prev, *next;
} DQNode;

void DQInit(DQNode *node);
void DQAdd(DQNode *base, DQNode *node);
void DQRemove(DQNode *node);
int DQCount(DQNode *node);

#endif


#ifndef NOTREAP

typedef struct TreapHead
{
	struct TreapHead *left, *right;
	int pri, key;
} TreapHead;

TreapHead *TrGet(TreapHead *root, int key);
void TrPut(TreapHead **root, TreapHead *node);
TreapHead *TrRemove(TreapHead **root, int key);
void TrEnum(TreapHead *root, void (*func)(TreapHead *node, void *clos), void *clos);
void tr_enum_afree(TreapHead *node, void *dummy);

#endif


#ifndef NOSTRINGCHUNK

/* string chunks: idea stolen from glib */

typedef struct StringChunk StringChunk;

StringChunk *SCAlloc(void);
char *SCAdd(StringChunk *chunk, const char *str);
void SCFree(StringChunk *chunk);

#endif


#ifndef NOMPQUEUE

#include "pthread.h"

/* message passing queue stuff */

typedef struct MPQueue
{
	LinkedList list;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
} MPQueue;

void MPInit(MPQueue *mpq);
void MPDestroy(MPQueue *mpq);
void MPAdd(MPQueue *mpq, void *data); /* will not block  */
void * MPTryRemove(MPQueue *mpq); /* will not block */
void * MPRemove(MPQueue *mpq); /* will block */

#endif /* MPQUEUE */


#endif
