
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifndef WIN32
#include <dlfcn.h>
#include <unistd.h>
#else
#include <direct.h>
#define dlopen(a,b) LoadLibrary(a)
#define dlerror() "dlerror() not supported"
#define dlsym(a,b) ((ModMain)GetProcAddress(a,b))
#define dlclose(a) FreeLibrary(a)
#endif

#include "asss.h"


#define MAXNAME 32


typedef void * ModuleHandle;


typedef struct ModuleData
{
	char name[MAXNAME];
	ModuleHandle hand;
	ModMain mm;
	int myself;
} ModuleData;



local int LoadMod(const char *);
local int UnloadModule(const char *);
local void EnumModules(void (*)(const char *, const char *, void *), void *);
local void AttachModule(const char *, int);
local void DetachModule(const char *, int);

local void RegInterface(void *iface, int arena);
local int UnregInterface(void *iface, int arena);
local void *GetInterface(const char *id, int arena);
local void *GetInterfaceByName(const char *name);
local void ReleaseInterface(void *iface);

local void RegCallback(const char *, void *, int);
local void UnregCallback(const char *, void *, int);
local LinkedList * LookupCallback(const char *, int);
local void FreeLookupResult(LinkedList *);

local void DoStage(int);
local void UnloadAllModules(void);


local HashTable *arenacallbacks, *globalcallbacks;
local HashTable *arenaints, *globalints, *intsbyname;

local LinkedList *mods;

local pthread_mutex_t modmtx = PTHREAD_MUTEX_INITIALIZER;
local pthread_mutex_t intmtx = PTHREAD_MUTEX_INITIALIZER;
local pthread_mutex_t cbmtx = PTHREAD_MUTEX_INITIALIZER;


local Imodman mmint =
{
	INTERFACE_HEAD_INIT(NULL, "modman")
	LoadMod, UnloadModule, EnumModules,
	AttachModule, DetachModule,
	RegInterface, UnregInterface, GetInterface, GetInterfaceByName, ReleaseInterface,
	RegCallback, UnregCallback, LookupCallback, FreeLookupResult,
	{ DoStage, UnloadAllModules }
};




/* return an entry point */

Imodman * InitModuleManager(void)
{
	mods = LLAlloc();
	arenacallbacks = HashAlloc(233);
	globalcallbacks = HashAlloc(233);
	arenaints = HashAlloc(43);
	globalints = HashAlloc(53);
	intsbyname = HashAlloc(23);
	mmint.head.refcount = 1;
	return &mmint;
}

void DeInitModuleManager(Imodman *mm)
{
	if (mods && LLGetHead(mods))
		fprintf(stderr, "All modules not unloaded!!!\n");
	if (mm && mm->head.refcount > 1)
		fprintf(stderr, "There are remaining references to the module manager!!!\n");
	LLFree(mods);
	HashFree(arenacallbacks);
	HashFree(globalcallbacks);
	HashFree(arenaints);
	HashFree(globalints);
	HashFree(intsbyname);
}


/* module management stuff */

#define DELIM ':'

int LoadMod(const char *_spec)
{
	char buf[PATH_MAX], spec[PATH_MAX], *modname, *filename, *path;
	int ret;
	ModuleData *mod;
	Ilogman *lm = GetInterface(I_LOGMAN, ALLARENAS);

	/* make copy of specifier */
	astrncpy(spec, _spec, PATH_MAX);

	if ((modname = strchr(spec, DELIM)))
	{
		filename = spec;
		*modname = 0;
		modname++;
	}
	else
	{
		modname = spec;
		filename = "internal";
	}

	if (lm) lm->Log(L_DRIVEL,"<module> Loading module '%s' from '%s'", modname, filename);

	mod = amalloc(sizeof(ModuleData));

	if (!strcasecmp(filename, "internal") || !strcasecmp(filename, "int"))
	{
#ifndef WIN32
		path = NULL;
#else
		/* Windows LoadLibrary function will not return itself if null,
		 * so need to set it to myself */
		strcpy(buf, &GetCommandLine()[1]);
		{
			char *quote = strchr(buf, '"');
			if (quote)
				*quote=0;
		}
		path = buf;
#endif
		mod->myself = 1;
	}
	else if (filename[0] == '/')
	{
		/* filename is an absolute path */
		path = filename;
	}
	else
	{
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		path = buf;
#ifndef WIN32
		if (snprintf(path, 256, "%s/bin/%s.so", cwd, filename) > 256)
#else
		if (snprintf(path, 256, "%s/bin/%s.dll", cwd, filename) > 256)
#endif
			goto die;
	}

	mod->hand = dlopen(path, RTLD_NOW);
	if (!mod->hand)
	{
		if (lm) lm->Log(L_ERROR,"<module> LoadMod: error in dlopen: %s", dlerror());
		goto die;
	}

	snprintf(buf, PATH_MAX, "MM_%s", modname);
	mod->mm = dlsym(mod->hand, buf);
	if (!mod->mm)
	{
		if (lm) lm->Log(L_ERROR,"<module> LoadMod: error in dlsym: %s", dlerror());
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	astrncpy(mod->name, modname, MAXNAME);

	ret = mod->mm(MM_LOAD, &mmint, ALLARENAS);

	if (ret != MM_OK)
	{
		if (lm) lm->Log(L_ERROR,
				"<module> Error loading module '%s'", modname);
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	pthread_mutex_lock(&modmtx);
	LLAdd(mods, mod);
	pthread_mutex_unlock(&modmtx);

	return MM_OK;

die:
	afree(mod);
	if (lm) ReleaseInterface(lm);
	return MM_FAIL;
}


local int UnloadModuleByPtr(ModuleData *mod)
{
	if (mod)
	{
		if (mod->mm)
			if ((mod->mm)(MM_UNLOAD, &mmint, ALLARENAS) == MM_FAIL)
				return MM_FAIL;
		if (mod->hand && !mod->myself) dlclose(mod->hand);
		pthread_mutex_lock(&modmtx);
		LLRemove(mods, mod);
		pthread_mutex_unlock(&modmtx);
		afree(mod);
	}
	return MM_OK;
}


local ModuleData *GetModuleByName(const char *name)
{
	ModuleData *mod;
	Link *l;

	pthread_mutex_lock(&modmtx);
	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		if (!strcasecmp(mod->name,name))
		{
			pthread_mutex_unlock(&modmtx);
			return mod;
		}
	}
	pthread_mutex_unlock(&modmtx);
	return NULL;
}


int UnloadModule(const char *name)
{
	return UnloadModuleByPtr(GetModuleByName(name));
}


local void RecursiveUnload(Link *l)
{
	if (l)
	{
		RecursiveUnload(l->next);
		UnloadModuleByPtr((ModuleData*) l->data);
	}
}


void DoStage(int stage)
{
	Link *l;
	pthread_mutex_lock(&modmtx);
	for (l = LLGetHead(mods); l; l = l->next)
		((ModuleData*)l->data)->mm(stage, &mmint, ALLARENAS);
	pthread_mutex_unlock(&modmtx);
}


void UnloadAllModules(void)
{
	RecursiveUnload(LLGetHead(mods));
	LLFree(mods);
	mods = NULL;
}


void EnumModules(void (*func)(const char *, const char *, void *), void *clos)
{
	ModuleData *mod;
	Link *l;
	pthread_mutex_lock(&modmtx);
	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		func(mod->name, NULL, clos);
	}
	pthread_mutex_unlock(&modmtx);
}


void AttachModule(const char *name, int arena)
{
	ModuleData *mod = GetModuleByName(name);
	if (mod)
		mod->mm(MM_ATTACH, &mmint, arena);
}

void DetachModule(const char *name, int arena)
{
	ModuleData *mod = GetModuleByName(name);
	if (mod)
		mod->mm(MM_DETACH, &mmint, arena);
}


/* interface management stuff */

void RegInterface(void *iface, int arena)
{
	const char *id;
	InterfaceHead *head = (InterfaceHead*)iface;

	assert(iface);
	assert(head->magic == MODMAN_MAGIC);

	id = head->iid;

	pthread_mutex_lock(&intmtx);

	HashAdd(intsbyname, head->name, iface);
	if (arena == ALLARENAS)
		HashAdd(globalints, id, iface);
	else
	{
		char key[64];
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		HashAdd(arenaints, key, iface);
	}

	pthread_mutex_unlock(&intmtx);

	head->refcount = 0;
}

int UnregInterface(void *iface, int arena)
{
	const char *id;
	InterfaceHead *head = (InterfaceHead*)iface;

	assert(head->magic == MODMAN_MAGIC);

	id = head->iid;

	if (head->refcount > 0)
		return head->refcount;

	pthread_mutex_lock(&intmtx);

	HashRemove(intsbyname, head->name, iface);
	if (arena == ALLARENAS)
		HashRemove(globalints, id, iface);
	else
	{
		char key[64];
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		HashRemove(arenaints, key, iface);
	}

	pthread_mutex_unlock(&intmtx);

	return 0;
}


/* must call holding intmtx */
local inline InterfaceHead *get_int(HashTable *hash, const char *id)
{
	InterfaceHead *head;
	head = HashGetOne(hash, id);
	if (head && head->priority != -1)
	{
		/* ok, we can't use the fast path. we have to try to find
		 * the best one now. */
		int bestpri = -1;
		LinkedList *lst;
		Link *l;

		lst = HashGet(hash, id);
		for (l = LLGetHead(lst); l; l = l->next)
			if (((InterfaceHead*)l->data)->priority > bestpri)
			{
				head = l->data;
				bestpri = head->priority;
			}
		LLFree(lst);
	}
	return head;
}


void * GetInterface(const char *id, int arena)
{
	InterfaceHead *head;

	pthread_mutex_lock(&intmtx);
	if (arena == ALLARENAS)
		head = get_int(globalints, id);
	else
	{
		char key[64];
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		head = get_int(arenaints, key);
		/* if the arena doesn't have it, fall back to a global one */
		if (!head)
			head = get_int(globalints, id);
	}
	pthread_mutex_unlock(&intmtx);
	if (head)
		head->refcount++;
	return head;
}


void * GetInterfaceByName(const char *name)
{
	InterfaceHead *head;
	pthread_mutex_lock(&intmtx);
	head = HashGetOne(intsbyname, name);
	pthread_mutex_unlock(&intmtx);
	if (head)
		head->refcount++;
	return head;
}


void ReleaseInterface(void *iface)
{
	InterfaceHead *head = (InterfaceHead*)iface;
	if (!iface) return;
	assert(head->magic == MODMAN_MAGIC);
	head->refcount--;
}


/* callback stuff */

void RegCallback(const char *id, void *f, int arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashAdd(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		HashAdd(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

void UnregCallback(const char *id, void *f, int arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashRemove(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		HashRemove(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

LinkedList * LookupCallback(const char *id, int arena)
{
	LinkedList *ll;

	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		ll = HashGet(globalcallbacks, id);
	}
	else
	{
		char key[64];
		/* first get global ones */
		ll= HashGet(globalcallbacks, id);
		/* then append local ones */
		key[0] = arena + ' ';
		astrncpy(key + 1, id, 63);
		HashGetAppend(arenacallbacks, key, ll);
	}
	pthread_mutex_unlock(&cbmtx);
	return ll;
}

void FreeLookupResult(LinkedList *lst)
{
	LLFree(lst);
}




