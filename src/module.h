
#ifndef __MODMAN_H
#define __MODMAN_H


/*
 * Imodman - the module and interface manager
 */

#include "util.h"

typedef struct Imodman
{
	int (*LoadModule)(char *specifier);
	/* load a module. the specifier is of the form 'file:modname'. file
	 * is the filename (without the .so) or 'int' for internal modules.
	 */

	void (*ReportFailedRequire)(char *module, char *variable);
	/* modules should call this when an interface that they require for
	 * initialization isn't provided. this is for informative purposes
	 * only: it will log an appropriate message. it's mean to be used by
	 * the macro REQUIRE. */

	void (*UnloadModule)(char *name);
	/* unloads a module. only the name should be given (not the file).
	 */

	void (*UnloadAllModules)();
	/* unloads all modules (in reverse order). this is only called by
	 * main to clean up before shutting down. */

	void (*AttachModule)(char *modname, int arena);
	void (*DetachModule)(char *modname, int arena);
	/* these are called by the arena manager to attach and detach
	 * modules to arenas that are loaded and destroyed. */

	void (*RegInterest)(int id, void *intpointer);
	void (*UnregInterest)(int id, void *intpointer);
	/* these are the primary way of getting access to an interface. you
	 * should call RegInterest with the interface you are interested in
	 * and a pointer to a pointer that will hold the interface address.
	 * it will be updated automatically if the modules that provides the
	 * interface is unloaded, or if another module overrides the
	 * interface. */

	void (*RegInterface)(int id, void *iface);
	void (*UnregInterface)(int id, void *iface);
	/* these are the way of providing interfaces for other modules. they
	 * should be called with an interface id and a pointer to the
	 * interface. */

	void (*RegCallback)(char *id, void *func, int arena);
	void (*UnregCallback)(char *id, void *func, int arena);
	/* these manage callback functions. putting this functionality in
	 * here keeps every other module that wants to call callbacks from
	 * implementing it themselves. */

	LinkedList * (*LookupCallback)(char *id, int arena);
	void (*FreeLookupResult)(LinkedList *lst);
	/* these are how callbacks are called. LookupCallback will return a
	 * list of functions to call. when you're done calling them all,
	 * call FreeLookupResult on the list. */

#define ALLARENAS (-1)
	/* if you want a callback to take effect globally, use ALLARENAS as
	 * the 'arena' parameter to the above functions. callbacks
	 * registered with ALLARENAS will be returned to _any_ call to
	 * LookupCallback with that callback name. calling LookupCallback
	 * with ALLARENAS will return _only_ the callbacks that were
	 * specifically registered with ALLARENAS. (that is, it doesn't
	 * return callbacks that are specific to an arena. if you think the
	 * behaviour doesn't make sense, tell me.) */

	int (*FindPlayer)(char *name);
	/* this is a useful function that doesn't belong here. but it is
	 * anyway. */

	char *desc;
	/* um.. ignore this for now. */
} Imodman;


typedef int (*ModMain)(int action, Imodman *mm);
/* all module entry points must be of this type */


Imodman * InitModuleManager();


/* nice macros to set interface variables */

/* use this one if you want an interface, but don't need it for
 * initialization */
#define WANTIFACE(iface, var) \
	mm->RegInterest(iface, &(var));

/* use this one if you need the interface for initialization. it will
 * fail if the interface isn't available. */
#define NEEDIFACE(iface, var) \
	mm->RegInterest(iface, &(var)); \
	if (!(var)) \
	{ \
		mm->ReportFailedRequire(MODULE, #var); \
		mm->UnregInterest(iface, &(var)); \
		return MM_FAIL; \
	}

/* use this in the unload section */
#define FORGETIFACE(iface, var) \
	mm->UnregInterest(iface, &(var));

#endif

