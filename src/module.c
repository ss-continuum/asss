
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
	int (*mm)(int, Imodman *);
	int myself;
} ModuleData;



local int LoadModule(char *);
local void UnloadAllModules();
local void UnloadModule(char *);
local void RegInterest(int, void*);
local void UnregInterest(int, void*);
local void RegInterface(int, void *);
local void UnregInterface(int, void *);
local void RegCallback(char *, void *);
local void UnregCallback(char *, void *);
local LinkedList * LookupCallback(char *);
local void FreeLookupResult(LinkedList *);


local int FindPlayer(char *);


/* THIS IS THE GLOBAL PLAYER ARRAY!!! */
local PlayerData players[MAXPLAYERS+EXTRA_PID_COUNT];

local HashTable *callbacks;

local LinkedList *mods;

local void *ints[MAXINTERFACE];
local LinkedList intupdates[MAXINTERFACE];


local Imodman mmint =
{
	LoadModule, UnloadModule, UnloadAllModules,
	RegInterest, UnregInterest, RegInterface, UnregInterface,
	RegCallback, UnregCallback, LookupCallback, FreeLookupResult,
	FindPlayer, players, NULL
};




/* return an entry point */

Imodman * InitModuleManager()
{
	int i;
	mods = LLAlloc();
	callbacks = HashAlloc(233);
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
		if (log) log->Log(LOG_ERROR,"Bad module locator string");
		return MM_FAIL;
	}

	if (log) log->Log(LOG_DEBUG,"Loading module '%s' from '%s'", modname, filename);


	mod = amalloc(sizeof(ModuleData));

	if (!strcasecmp(filename,"internal") || !strcasecmp(filename,"int"))
	{
		name = (char*)NULL;
		mod->myself = 1;
	}
	else
	{
		getcwd(name, 256);
		strcat(name, "/bin/"); strcat(name, filename); strcat(name, ".so");
	}
	
	mod->hand = dlopen(name, RTLD_NOW);
	if (!mod->hand)
	{
		if (log) log->Log(LOG_ERROR,"LoadModule: error in dlopen");
		afree(mod);
		return MM_FAIL;
	}

	name = _buf;
	sprintf(name, "MM_%s", modname);
	mod->mm = dlsym(mod->hand, name);
	if (!mod->mm)
	{
		if (log) log->Log(LOG_ERROR,"LoadModule: error in dlsym");
		if (!mod->myself) dlclose(mod->hand);
		afree(mod);
		return MM_FAIL;
	}

	astrncpy(mod->name, modname, MAXNAME-2);
	modname--; *modname = DELIM;

	ret = mod->mm(MM_LOAD, &mmint);

	if (ret != MM_OK)
	{
		if (log) log->Log(LOG_ERROR,
				"Error loading module string '%s'", filename);
		if (!mod->myself) dlclose(mod->hand);
		afree(mod);
		return ret;
	}
	
	LLAdd(mods, mod);
	return MM_OK;
}


local void UnloadModuleByPtr(ModuleData *mod)
{
	printf("Unloading module '%s'\n", mod->name);
/*	if (ints[I_LOGMAN]) */
/*		((Ilogman*)ints[I_LOGMAN])->Log(LOG_DEBUG, "Unloading module %s", mod->name); */
	if (mod->mm) (mod->mm)(MM_UNLOAD, &mmint);
	if (mod->hand && !mod->myself) dlclose(mod->hand);
	afree(mod);
}


void UnloadModule(char *name)
{
	ModuleData *mod;
	Link *l;

	for (l = LLGetHead(mods); l; l = l->next)
	{
		mod = (ModuleData*) l->data;
		if (!strcasecmp(mod->name,name))
		{
			UnloadModuleByPtr(mod);
			LLRemove(mods, mod);
			return;
		}
	}
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

void RegCallback(char *id, void *f)
{
	HashAdd(callbacks, id, f);
}

void UnregCallback(char *id, void *f)
{
	HashRemove(callbacks, id, f);
}

LinkedList * LookupCallback(char *id)
{
	return HashGet(callbacks, id);
}

void FreeLookupResult(LinkedList *lst)
{
	LLFree(lst);
}


/* misc helper function */

int FindPlayer(char *name)
{
	int i;

	for (i = 0; i < MAXPLAYERS; i++)
		if (	players[i].status == S_CONNECTED &&
				strcasecmp(name, players[i].name) == 0)
			return i;
	return -1;
}



