
/* dist: public */

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
	const char *info;
	int myself;
} ModuleData;


local int LoadMod(const char *);
local int UnloadModule(const char *);
local void EnumModules(void (*)(const char *, const char *, void *), void *);
local void AttachModule(const char *, Arena *);
local void DetachModule(const char *, Arena *);

local void RegInterface(void *iface, Arena *arena);
local int UnregInterface(void *iface, Arena *arena);
local void *GetInterface(const char *id, Arena *arena);
local void *GetInterfaceByName(const char *name);
local void ReleaseInterface(void *iface);

local void RegCallback(const char *, void *, Arena *);
local void UnregCallback(const char *, void *, Arena *);
local void LookupCallback(const char *, Arena *, LinkedList *);
local void FreeLookupResult(LinkedList *);

local void DoStage(int);
local void UnloadAllModules(void);
local void NoMoreModules(void);


local HashTable *arenacallbacks, *globalcallbacks;
local HashTable *arenaints, *globalints, *intsbyname;
local LinkedList *mods;
local int nomoremods;

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
	{ DoStage, UnloadAllModules, NoMoreModules }
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
	nomoremods = 0;
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

int LoadMod(const char *_spec)
{
	char buf[PATH_MAX], spec[PATH_MAX], *modname, *filename, *path;
	int ret;
	ModuleData *mod;
	Ilogman *lm;

	if (nomoremods) return MM_FAIL;

	lm = GetInterface(I_LOGMAN, ALLARENAS);

	/* make copy of specifier */
	astrncpy(spec, _spec, PATH_MAX);

	if ((modname = strchr(spec, ':')))
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

	if (lm) lm->Log(L_INFO, "<module> Loading module '%s' from '%s'", modname, filename);

	mod = amalloc(sizeof(ModuleData));

	if (!strcasecmp(filename, "internal"))
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
#ifdef CFG_RESTRICT_MODULE_PATH
	else if (strstr(filename, "..") || filename[0] == '/')
	{
		if (lm)
			lm->Log(L_ERROR, "<module> refusing to load filename: %s",
					filename);
		else
			fprintf(stderr, "%c <module> refusing to load filename: %s",
					L_ERROR, filename);
		goto die;
	}
#else
	else if (filename[0] == '/')
	{
		/* filename is an absolute path */
		path = filename;
	}
#endif
	else
	{
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		path = buf;
		if (snprintf(path, sizeof(buf), "%s/bin/%s"
#ifndef WIN32
					".so"
#else
					".dll"
#endif
					, cwd, filename) > sizeof(buf))
			goto die;
	}

	mod->hand = dlopen(path, RTLD_NOW);
	if (!mod->hand)
	{
#ifndef WIN32
		if (lm) lm->Log(L_ERROR,"<module> Error in dlopen: %s", dlerror());
#else
		LPVOID lpMsgBuf;

		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0,
				NULL
		);
		if (lm)
			lm->Log(L_ERROR, "<module> Error in LoadLibrary: %s", (LPCTSTR)lpMsgBuf);
		else
			fprintf(stderr, "%c <module> Error in LoadLibrary: %s", L_ERROR, (LPCTSTR)lpMsgBuf);
		LocalFree(lpMsgBuf);
#endif
		goto die;
	}

	snprintf(buf, PATH_MAX, "MM_%s", modname);
	mod->mm = (ModMain)dlsym(mod->hand, buf);
	if (!mod->mm)
	{
#ifndef WIN32
		if (lm) lm->Log(L_ERROR,"<module> Error in dlsym: %s", dlerror());
#else
		LPVOID lpMsgBuf;
		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0,
				NULL
		);
		if (lm)
			lm->Log(L_ERROR, "<module> Error in GetProcAddress: %s", (LPCTSTR)lpMsgBuf);
		else
			fprintf(stderr, "%c <module> Error in GetProcAddress: %s", L_ERROR, (LPCTSTR)lpMsgBuf);
		LocalFree(lpMsgBuf);
#endif
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	/* load info if it exists */
	snprintf(buf, PATH_MAX, "info_%s", modname);
	mod->info = dlsym(mod->hand, buf);

	astrncpy(mod->name, modname, MAXNAME);

	ret = mod->mm(MM_LOAD, &mmint, ALLARENAS);

	if (ret != MM_OK)
	{
		if (lm)
			lm->Log(L_ERROR, "<module> Error loading module '%s'", modname);
		else
			printf("%c <module> Error loading module '%s'\n", L_ERROR, modname);
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	pthread_mutex_lock(&modmtx);
	LLAdd(mods, mod);
	pthread_mutex_unlock(&modmtx);
	if (lm) ReleaseInterface(lm);

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
	ModuleData *mod = GetModuleByName(name);
	return mod ? UnloadModuleByPtr(mod) : MM_FAIL;
}


local void RecursiveUnload(Link *l)
{
	if (l)
	{
		RecursiveUnload(l->next);
		if (UnloadModuleByPtr((ModuleData*) l->data) == MM_FAIL)
			fprintf(stderr, "E <module> Error unloading module %s\n",
					((ModuleData*)(l->data))->name);
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
	LLEmpty(mods);
}


void NoMoreModules(void)
{
	nomoremods = 1;
}


void EnumModules(void (*func)(const char *, const char *, void *), void *clos)
{
	ModuleData *mod;
	Link *l;
	pthread_mutex_lock(&modmtx);
	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		func(mod->name, mod->info, clos);
	}
	pthread_mutex_unlock(&modmtx);
}


void AttachModule(const char *name, Arena *arena)
{
	ModuleData *mod = GetModuleByName(name);
	if (mod)
		mod->mm(MM_ATTACH, &mmint, arena);
}

void DetachModule(const char *name, Arena *arena)
{
	ModuleData *mod = GetModuleByName(name);
	if (mod)
		mod->mm(MM_DETACH, &mmint, arena);
}


/* interface management stuff */

void RegInterface(void *iface, Arena *arena)
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
		snprintf(key, 64, "%p-%s", (void*)arena, id);
		HashAdd(arenaints, key, iface);
	}

	pthread_mutex_unlock(&intmtx);

	head->refcount = 0;
}

int UnregInterface(void *iface, Arena *arena)
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
		snprintf(key, 64, "%p-%s", (void*)arena, id);
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
		LinkedList lst = LL_INITIALIZER;
		Link *l;

		HashGetAppend(hash, id, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
			if (((InterfaceHead*)l->data)->priority > bestpri)
			{
				head = l->data;
				bestpri = head->priority;
			}
		LLEmpty(&lst);
	}
	return head;
}


void * GetInterface(const char *id, Arena *arena)
{
	InterfaceHead *head;

	pthread_mutex_lock(&intmtx);
	if (arena == ALLARENAS)
		head = get_int(globalints, id);
	else
	{
		char key[64];
		snprintf(key, 64, "%p-%s", (void*)arena, id);
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

void RegCallback(const char *id, void *f, Arena *arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashAdd(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		snprintf(key, 64, "%p-%s", (void*)arena, id);
		HashAdd(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

void UnregCallback(const char *id, void *f, Arena *arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashRemove(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		snprintf(key, 64, "%p-%s", (void*)arena, id);
		HashRemove(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

void LookupCallback(const char *id, Arena *arena, LinkedList *ll)
{
	LLInit(ll);
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashGetAppend(globalcallbacks, id, ll);
	}
	else
	{
		char key[64];
		/* first get global ones */
		HashGetAppend(globalcallbacks, id, ll);
		/* then append local ones */
		snprintf(key, 64, "%p-%s", (void*)arena, id);
		HashGetAppend(arenacallbacks, key, ll);
	}
	pthread_mutex_unlock(&cbmtx);
}

void FreeLookupResult(LinkedList *lst)
{
	LLEmpty(lst);
}


