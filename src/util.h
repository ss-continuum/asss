
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

char *astrncpy(char *dest, const char *source, size_t destlength);

void Error(int errorcode, char *message, ...);


/* list manipulation functions */

typedef struct Link
{
	struct Link *next;
	void *data;
} Link;

typedef struct LinkedList LinkedList;

LinkedList * LLAlloc();
void LLFree(LinkedList *);
void LLAdd(LinkedList *, void *);
int LLRemove(LinkedList *, void *);
Link *LLGetHead(LinkedList *);


/* hashing stuff */

typedef struct HashTable HashTable;

HashTable * HashAlloc();
void HashFree(HashTable *);
void HashEnum(HashTable *, void (*)(void *));
void HashAdd(HashTable *, const char *, void *);
void HashRemove(HashTable *, const char *, void *);
LinkedList *HashGet(HashTable *, const char *);



#endif

