
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



local int LoadModule(char *);
local void ReportFailedRequire(char *, char *);
local void UnloadAllModules();
local void UnloadModule(char *);
local void AttachModule(char *, int);
local void DetachModule(char *, int);
local void RegInterest(int, void*);
local void UnregInterest(int, void*);
local void RegInterface(int, void *);
local void UnregInterface(int, void *);
local void RegCallback(char *, void *, int);
local void UnregCallback(char *, void *, int);
local LinkedList * LookupCallback(char *, int);
local void FreeLookupResult(LinkedList *);


local int FindPlayer(char *);


local HashTable *arenacallbacks, *globalcallbacks;

local LinkedList *mods;

local void *ints[MAXINTERFACE];
local LinkedList intupdates[MAXINTERFACE];


local Imodman mmint =
{
	LoadModule, ReportFailedRequire, UnloadModule, UnloadAllModules,
	AttachModule, DetachModule,
	RegInterest, UnregInterest, RegInterface, UnregInterface,
	RegCallback, UnregCallback, LookupCallback, FreeLookupResult,
	FindPlayer, NULL
};




/* return an entry point */

Imodman * InitModuleManager()
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

int LoadModule(char *filename)
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
		name = (char*)NULL;
		mod->myself = 1;
	}
	else if (strchr(filename, '/'))
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
		if (log) log->Log(L_ERROR,"<module> LoadModule: error in dlopen");
		goto die;
	}

	name = _buf;
	sprintf(name, "MM_%s", modname);
	mod->mm = dlsym(mod->hand, name);
	if (!mod->mm)
	{
		if (log) log->Log(L_ERROR,"<module> LoadModule: error in dlsym");
		if (!mod->myself) dlclose(mod->hand);
		goto die;
	}

	astrncpy(mod->name, modname, MAXNAME-2);
	modname--; *modname = DELIM; modname++;

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


void ReportFailedRequire(char *mod, char *req)
{
	Ilogman *log = ints[I_LOGMAN];
	if (log)
		log->Log(L_ERROR, "<module> Module '%s' couldn't find interface for '%s'", mod, req);
	else
		printf("<module> Module '%s' couldn't find interface for '%s'", mod, req);
}


local void UnloadModuleByPtr(ModuleData *mod)
{
	if (mod)
	{
		if (mod->mm) (mod->mm)(MM_UNLOAD, &mmint, ALLARENAS);
		if (mod->hand && !mod->myself) dlclose(mod->hand);
		afree(mod);
	}
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


void UnloadModule(char *name)
{
	UnloadModuleByPtr(GetModuleByName(name));
}


local void RecursiveUnload(Link *l)
{
	if (l)
	{
		RecursiveUnload(l->next);
		UnloadModuleByPtr((ModuleData*) l->data);
	}
}

void UnloadAllModules()
{
	RecursiveUnload(LLGetHead(mods));
	LLFree(mods);
}

void AttachModule(char *name, int arena)
{
	ModuleData *mod = GetModuleByName(name);
	if (mod)
		mod->mm(MM_ATTACH, &mmint, arena);
}

void DetachModule(char *name, int arena)
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

void RegCallback(char *id, void *f, int arena)
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

void UnregCallback(char *id, void *f, int arena)
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


/* misc helper function */

int FindPlayer(char *name)
{
	int i;
	Iplayerdata *pd;
	PlayerData *p;

	pd = ints[I_PLAYERDATA];

	if (!pd)
		return -1;

	pd->LockStatus();
	for (i = 0, p = pd->players; i < MAXPLAYERS; i++, p++)
		if (	p->status == S_CONNECTED &&
				strcasecmp(name, p->name) == 0)
		{
			pd->UnlockStatus();
			return i;
		}
	pd->UnlockStatus();
	return -1;
}



