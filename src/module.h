
#ifndef __MODMAN_H
#define __MODMAN_H


/*
 * Imodman - the module and interface manager
 */

#include "util.h"


typedef struct Imodman Imodman;


Imodman * InitModuleManager(void);
/* this is the entry point to the module manager. only main should call
 * this. */


typedef int (*ModMain)(int action, Imodman *mm, int arena);
/* all module entry points must be of this type */


/* action codes for module main functions */

#define MM_LOAD       1
/* this means the module is being loaded. do all global initialization
 * here. */

#define MM_UNLOAD     2
/* the module is being unloaded. try to clean up as best as possible. */

#define MM_ATTACH     3
/* the module is being attached to an arena. if you have any
 * arena-specific functionality, now would be a good time to turn it on
 * for this arena. */

#define MM_DETACH     4
/* the reverse of the above. disable any special functionality for this
 * arena. */

#define MM_CHECKBUILD 5
/* this is used to check runtime compatability of interfaces. all
 * modules should respond to this by returning the value of the
 * preprocessor constant BUILDNUMBER. */


/* return values for ModMain functions */
#define MM_FAIL 1
#define MM_OK   0


struct Imodman
{
	int (*LoadModule)(const char *specifier);
	/* load a module. the specifier is of the form 'file:modname'. file
	 * is the filename (without the .so) or 'int' for internal modules.
	 */

	int (*UnloadModule)(const char *name);
	/* unloads a module. only the name should be given (not the file). */

	void (*UnloadAllModules)(void);
	/* unloads all modules (in reverse order). this is only called by
	 * main to clean up before shutting down. */

	void (*EnumModules)(void (*func)(const char *name, const char *info,
				void *clos), void *clos);
	/* calls the given function for each loaded module, passing it the
	 * module name any extra info, and a closure pointer for it to use.
	 */

	void (*AttachModule)(const char *modname, int arena);
	void (*DetachModule)(const char *modname, int arena);
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

	void (*RegCallback)(const char *id, void *func, int arena);
	void (*UnregCallback)(const char *id, void *func, int arena);
	/* these manage callback functions. putting this functionality in
	 * here keeps every other module that wants to call callbacks from
	 * implementing it themselves. */

	LinkedList * (*LookupCallback)(const char *id, int arena);
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
};


/* this might be a useful macro */
#define DO_CBS(cb, arena, type, args)                  \
do {                                                   \
	LinkedList *lst = mm->LookupCallback(cb, arena);   \
	Link *l;                                           \
	for (l = LLGetHead(lst); l; l = l->next)           \
		((type)l->data) args ;                         \
	mm->FreeLookupResult(lst);                         \
} while (0)


#endif

