
/* dist: public */

#ifndef __MODMAN_H
#define __MODMAN_H


/*
 * Imodman - the module and interface manager
 */

#include "util.h"

struct Imodman;

/* module load/unload operations */
enum
{
	MM_LOAD,
	/* this means the module is being loaded. do all global
	 * initialization here. */

	MM_POSTLOAD,
	/* this is a second initialization phase that allows modules to
	 * obtain references to interfaces exported by modules loaded after
	 * them. interfaces obtained in postload should be released in
	 * preload, so that module unloading can proceed cleanly. */

	MM_PREUNLOAD,
	/* this stage is for cleaning up any activity done in the postload
	 * stage. */

	MM_UNLOAD,
	/* the module is being unloaded. try to clean up as best as
	 * possible. */

	MM_ATTACH,
	/* the module is being attached to an arena. if you have any
	 * arena-specific functionality, now would be a good time to turn it
	 * on for this arena. */

	MM_DETACH
	/* the reverse of the above. disable any special functionality for
	 * this arena. */
};


/* return values for module functions */
#define MM_OK     0
#define MM_FAIL   1


/* all interfaces declarations MUST start with this macro */
#define INTERFACE_HEAD_DECL struct InterfaceHead head;

/* and all interface initializers must start with one of these macros */
#define INTERFACE_HEAD_INIT(iid, name) { MODMAN_MAGIC, iid, name, -1, 0 },
#define INTERFACE_HEAD_INIT_PRI(iid, name, pri) { MODMAN_MAGIC, iid, name, pri, 0 },


/* stuff used for implementing the above */
typedef struct InterfaceHead
{
	unsigned long magic;
	const char *iid, *name;
	int priority, refcount;
} InterfaceHead;

#define MODMAN_MAGIC 0x46692017


typedef struct mod_args_t
{
	char name[32];
	const char *info;
	void *privdata;
} mod_args_t;

typedef int (*ModuleLoaderFunc)(int action, mod_args_t *args, const char *line, Arena *arena);
/* this will be called when loading a module. action is:
 * MM_LOAD - requesting to load a module. line will be set. fill in
 * args. ignore arena. return MM_OK/FAIL
 * MM_UNLOAD - requesting to unload. ignore line. args will be set.
 * return MM_OK/FAIL.
 * MM_ATTACH - requesting to attach. args and arena will be set. ignore
 * line.
 * MM_DETACH - requesting to detach. args and arena will be set. ignore
 * line.
 * MM_POSTLOAD, MM_PREUNLOAD - two more phases of initialization. don't
 * worry about these.
 *
 * line, when it is set, is a module specifier (from modules.conf or
 * ?insmod).
 * all of the stuff in args is for the module loader's use, although
 * name and info will be used by the module manager.
 */


typedef struct Imodman
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* module stuff */

	int (*LoadModule)(const char *specifier);
	/* load a module. the specifier is of the form 'file:modname'. file
	 * is the filename (without the .so/.dll) or 'int' for internal modules.
	 * eventually, 'file:modname@remotehost:port' will be supported for
	 * remote modules.  */
	/* pyint: string -> int */

	int (*UnloadModule)(const char *name);
	/* unloads a module. only the name should be given (not the file). */
	/* pyint: string -> int */

	void (*EnumModules)(void (*func)(const char *name, const char *info,
				void *clos), void *clos);
	/* calls the given function for each loaded module, passing it the
	 * module name any extra info, and a closure pointer for it to use.
	 */

	void (*AttachModule)(const char *modname, Arena *arena);
	void (*DetachModule)(const char *modname, Arena *arena);
	/* these are called by the arena manager to attach and detach
	 * modules to arenas that are loaded and destroyed. */


	/* interface stuff */

	void (*RegInterface)(void *iface, Arena *arena);
	int (*UnregInterface)(void *iface, Arena *arena);
	/* these are the way of providing interfaces for other modules. they
	 * should be called with an interface id and a pointer to the
	 * interface. UnregInterface will refuse to unregister an interface
	 * that is references by other modules. it will return the reference
	 * count of the interface that's being unregistered, so a zero means
	 * success. */

	void * (*GetInterface)(const char *id, Arena *arena);
	void * (*GetInterfaceByName)(const char *name);
	/* these two retrieve interface pointers. GetInterface gets the
	 * interface pointer of the highest-priority implementation for that
	 * id. GetInterfaceByName gets one specific implementation by name.
	 */

	void (*ReleaseInterface)(void *iface);
	/* this should be called on an interface pointer when you don't need
	 * it anymore. */


	/* callback stuff */

	void (*RegCallback)(const char *id, void *func, Arena *arena);
	void (*UnregCallback)(const char *id, void *func, Arena *arena);
	/* these manage callback functions. putting this functionality in
	 * here keeps every other module that wants to call callbacks from
	 * implementing it themselves. */

	void (*LookupCallback)(const char *id, Arena *arena, LinkedList *res);
	void (*FreeLookupResult)(LinkedList *res);
	/* these are how callbacks are called. LookupCallback will return a
	 * list of functions to call. when you're done calling them all,
	 * call FreeLookupResult on the list. */

	Arena * (*GetArenaOfCurrentCallback)(void);
	Arena * (*GetArenaOfLastInterfaceRequest)(void);
	/* does what they say. you shouldn't need to use these. */

#define ALLARENAS NULL
	/* if you want a callback to take effect globally, use ALLARENAS as
	 * the 'arena' parameter to the above functions. callbacks
	 * registered with ALLARENAS will be returned to _any_ call to
	 * LookupCallback with that callback name. calling LookupCallback
	 * with ALLARENAS will return _only_ the callbacks that were
	 * specifically registered with ALLARENAS. (that is, it doesn't
	 * return callbacks that are specific to an arena. if you think the
	 * behaviour doesn't make sense, tell me.) */

	/* module loaders */
	void (*RegModuleLoader)(const char *signature, ModuleLoaderFunc func);
	void (*UnregModuleLoader)(const char *signature, ModuleLoaderFunc func);

	/* these functions should be called only from main.c */
	struct
	{
		void (*DoStage)(int stage);
		void (*UnloadAllModules)(void);
		void (*NoMoreModules)(void);
	} frommain;
} Imodman;


Imodman * InitModuleManager(void);
/* this is the entry point to the module manager. only main should call
 * this. */

void DeInitModuleManager(Imodman *mm);
/* this deinitializes the module manager. only main should call this. */



/* this might be a useful macro */
#define DO_CBS(cb, arena, type, args)                  \
do {                                                   \
	LinkedList lst;                                    \
	Link *l;                                           \
	mm->LookupCallback(cb, arena, &lst);               \
	for (l = LLGetHead(&lst); l; l = l->next)          \
		((type)l->data) args ;                         \
	mm->FreeLookupResult(&lst);                        \
} while (0)



#endif

