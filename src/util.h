
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
 * RemoveCFLF strips CR's and LF's from strings. probably not too useful
 * except for file i/o (which you should let other modules handle).
 *
 * amalloc is how you should allocate memory. it will never return NULL.
 * same for astrdup. use free as normal to free memory from these.
 *
 * astrncpy should be used instead of strncpy. its interface makes
 * sense, as opposed to strncpy. the size is actually the length of the
 * destination buffer. at most length-1 characters will be copied and
 * the last byte will be null.
 *
 * you can use Error to signal a really terrible horrible error
 * condition that should stop the server. this one will call abort().
 * don't use unnecessarily.
 *
 * LinkedList:
 * use LLAlloc to get a list. LLAdd to add stuff to it (in the form of
 * void pointers). LLRemove to remove at most one matching item (so you
 * can have duplicates and remove them one at a time). LLRewind to set
 * the current item pointer to the start, and then LLNext to get the
 * next item, until it returns NULL (so you can't store NULL in the
 * lists). LLFree to free it.
 *
 * HashTable:
 * HashAlloc to get one. HashAdd and HashRemove to add and remove stuff.
 * HashGet will return a linked list with all the matching items. yes,
 * you can store duplicates in it and get them all out at once. the list
 * HashGet returns will be overwritten with ever call, so use it before
 * you call HashGet again.
 *
 */


/* include for size_t */
#include <stddef.h>


/* miscelaneous stuff */

unsigned int GTC();

char *RemoveCRLF(char *str);

void *amalloc(size_t bytes);
char *astrdup(const char *str);
void afree(void *ptr);

char *astrncpy(char *dest, const char *source, size_t destlength);

void Error(int errorcode, char *message, ...);


/* list manipulation functions */

typedef struct Link
{
	struct Link *next;
	void *data;
} Link;

struct LinkedList
{
	Link *start, *end;
};

typedef struct LinkedList LinkedList;

LinkedList * LLAlloc();
void LLInit(LinkedList *lst);
void LLFree(LinkedList *lst);
void LLAdd(LinkedList *lst, void *data);
int LLRemove(LinkedList *lst, void *data);
Link *LLGetHead(LinkedList *lst);


/* hashing stuff */

typedef struct HashTable HashTable;

HashTable * HashAlloc();
void HashFree(HashTable *ht);
void HashEnum(HashTable *ht, void (*func)(void *));
void HashAdd(HashTable *ht, const char *key, void *data);
void HashRemove(HashTable *ht, const char *key, void *data);
LinkedList *HashGet(HashTable *ht, const char *key);



#ifndef NOTHREAD

/* threading stuff */

#include <pthread.h>

typedef void * (*ThreadFunc)(void *);
typedef pthread_t Thread;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Condition;

Thread StartThread(ThreadFunc func, void *data);
void JoinThread(Thread th);
void InitMutex(Mutex *mtx);
void LockMutex(Mutex *mtx);
void UnlockMutex(Mutex *mtx);
void InitCondition(Condition *cond);
void SignalCondition(Condition *cond, int all);
void WaitCondition(Condition *cond, Mutex *mtx);

#endif


#ifndef NOMPQUEUE

/* mpqueue stuff */

typedef struct MPQueue
{
	LinkedList list;
	Mutex mtx;
	Condition cond;
} MPQueue;

void MPInit(MPQueue *mpq);
void MPDestroy(MPQueue *mpq);
void MPAdd(MPQueue *mpq, void *data); /* will not block  */
void * MPRemove(MPQueue *mpq); /* WILL BLOCK */

#endif


#endif

