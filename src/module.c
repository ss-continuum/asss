
#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

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



local int LoadModule(const char *);
local int UnloadModule(const char *);
local void UnloadAllModules(void);
local void EnumModules(void (*)(const char *, const char *, void *), void *);
local void AttachModule(const char *, int);
local void DetachModule(const char *, int);
local void RegInterest(int, void*);
local void UnregInterest(int, void*);
local void RegInterface(int, void *);
local void UnregInterface(int, void *);
local void RegCallback(const char *, void *, int);
local void UnregCallback(const char *, void *, int);
local LinkedList * LookupCallback(char *, int);
local void FreeLookupResult(LinkedList *);



local HashTable *arenacallbacks, *globalcallbacks;

local LinkedList *mods;

local void *ints[MAXINTERFACE];
local LinkedList intupdates[MAXINTERFACE];


local Imodman mmint =
{
	LoadModule, UnloadModule, UnloadAllModules, EnumModules,
	AttachModule, DetachModule,
	RegInterest, UnregInterest, RegInterface, UnregInterface,
	RegCallback, UnregCallback, LookupCallback, FreeLookupResult
};




/* return an entry point */

Imodman * InitModuleManager(void)
{
	int i;
	mods = LLAlloc();
	arenacallbacks = HashAlloc(233);
	globalcallbacks = HashAlloc(233);
	for (i = 0; i < MAXINTERFACE; i++)
	{
		LLInit(intupdates + i);
		ints[i] = NULL;
	}
	return &mmint;
}


/* module management stuff */

#define DELIM ':'

int LoadModule(const char *filename)
{
	static char _buf[256];
	ModuleData *mod;
	char *name = _buf, *modname;
	int ret;
	Ilogman *log = ints[I_LOGMAN]; /* hack: change this when implemetion changes */

	if ((modname = strchr(filename,DELIM)))
	{
		*modname = 0;
		modname++;
	}
	else
	{
		if (log) log->Log(L_ERROR,"<module> Bad module locator string");
		return MM_FAIL;
	}

	if (log) log->Log(L_DRIVEL,"<module> Loading module '%s' from '%s'", modname, filename);


	mod = amalloc(sizeof(ModuleData));

	if (!strcasecmp(filename,"internal") || !strcasecmp(filename,"int"))
	{
		name = NULL;
		mod->myself = 1;
	}
	else if (filename[0] == '/')
	{
		/* filename is an absolute path */
		astrncpy(name, filename, 256);
	}
	else
	{
		char cwd[256];
		getcwd(cwd, 256);
		if (snprintf(name, 256, "%s/bin/%s.so", cwd, filename) > 256)
			goto die;
	}

	mod->hand = dlopen(name, RTLD_NOW);
	if (!mod->hand)
	{
		if (log) log->Log(L_ERROR,"<module> LoadModule: error in dlopen: %s", dlerror());
		goto die;
	}

	name = _buf;
	sprintf(name, "MM_%s", modname);
	mod->mm = dlsym(mod->hand, name);
	if (!mod->mm)
	{
		if (log) log->Log(L_ERROR,"<module> LoadModule: error in dlsym: %s", dlerror());
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	astrncpy(mod->name, modname, MAXNAME-2);
	modname--; *modname = DELIM; modname++;

	ret = mod->mm(MM_CHECKBUILD, &mmint, -1);
	if (ret != BUILDNUMBER)
	{
		if (log) log->Log(L_ERROR,
				"<module> Build number mismatch: module '%s' was built with %d, we were built with %d",
				modname,
				ret,
				BUILDNUMBER);
		if (!mod->myself) dlclose(mod->hand);
		goto die2;
	}

	ret = mod->mm(MM_LOAD, &mmint, -1);

	if (ret != MM_OK)
	{
		if (log) log->Log(L_ERROR,
				"<module> Error loading module '%s'", modname);
		if (!mod->myself) dlclose(mod->hand);
		goto die2;
	}

	LLAdd(mods, mod);
	return MM_OK;

die:
	modname--; *modname = DELIM;
die2:
	afree(mod);
	return MM_FAIL;
}


local int UnloadModuleByPtr(ModuleData *mod)
{
	if (mod)
	{
		if (mod->mm) (mod->mm)(MM_UNLOAD, &mmint, ALLARENAS);
		if (mod->hand && !mod->myself) dlclose(mod->hand);
		afree(mod);
	}
	return MM_OK;
}


local ModuleData *GetModuleByName(char *name)
{
	ModuleData *mod;
	Link *l;
	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		if (!strcasecmp(mod->name,name))
			return mod;
	}
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

void UnloadAllModules(void)
{
	RecursiveUnload(LLGetHead(mods));
	LLFree(mods);
}


void EnumModules(void (*func)(const char *, const char *, void *), void *clos)
{
	ModuleData *mod;
	Link *l;
	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		func(mod->name, NULL, clos);
	}
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

void RegInterest(int ii, void *intp)
{
	if (ii >= 0 && ii < MAXINTERFACE)
	{
		LLAdd(intupdates + ii, intp);
		*((void**)intp) = ints[ii];
	}
}

void UnregInterest(int ii, void *intp)
{
	LLRemove(intupdates + ii, intp);
}


void RegInterface(int ii, void *face)
{
	if (ii >= 0 && ii < MAXINTERFACE)
	{
		Link *l;

		ints[ii] = face;

		for (l = LLGetHead(intupdates + ii); l; l = l->next)
			*((void**)l->data) = face;
	}
}

/* eventually: make this return int which is the ref count */
void UnregInterface(int ii, void *face)
{
	int c = 0;
	if (ints[ii] == face)
	{
		Link *l;

		for (l = LLGetHead(intupdates + ii); l; l = l->next)
			c++;
		if (c == 0)
			ints[ii] = NULL;
	}
	/* return c; */
}

void RegCallback(const char *id, void *f, int arena)
{
	if (arena == ALLARENAS)
	{
		HashAdd(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		snprintf(key, 64, "%d-%s", arena, id);
		HashAdd(arenacallbacks, key, f);
	}
}

void UnregCallback(const char *id, void *f, int arena)
{
	if (arena == ALLARENAS)
	{
		HashRemove(globalcallbacks, id, f);
	}
	else
	{
		char key[64];
		snprintf(key, 64, "%d-%s", arena, id);
		HashRemove(arenacallbacks, key, f);
	}
}

LinkedList * LookupCallback(char *id, int arena)
{
	if (arena == ALLARENAS)
	{
		return HashGet(globalcallbacks, id);
	}
	else
	{
		char key[64];
		LinkedList *l;
		/* first get global ones */
		l = HashGet(globalcallbacks, id);
		/* then append local ones */
		snprintf(key, 64, "%d-%s", arena, id);
		HashGetAppend(arenacallbacks, key, l);
		return l;
	}
}

void FreeLookupResult(LinkedList *lst)
{
	LLFree(lst);
}




