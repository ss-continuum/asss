
#ifndef __CLIENTSET_H
#define __CLIENTSET_H

/* Iclientset
 *
 * this is the interface to the module that manages the client-side
 * settings. it loads them from disk when the arena is loaded and when
 * Reconfigure is called. arenaman calls SendClientSettings as part of
 * the arena response procedure.
 */

typedef struct Iclientset
{
	void (*SendClientSettings)(int pid);
	void (*Reconfigure)(int arena);
} Iclientset;

#endif

