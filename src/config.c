
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <malloc.h>
#include <io.h>
#endif


#include "asss.h"

#include "pathutil.h"

#include "app.h"


/* structs */

struct Entry
{
	char *keystr, *val, *info;
};

struct ConfigFile
{
	int refcount;
	HashTable *thetable;
	StringChunk *thestrings;
	pthread_mutex_t mutex; /* this mutex is recursive */
	LinkedList dirty;
	ConfigChangedFunc changed;
	void *clos;
	time_t lastmod;
	char *filename, *arena, *name;
};


/* globals */

local ConfigHandle global;

local HashTable *opened;
local LinkedList files;
local pthread_mutex_t cfgmtx; /* protects opened and files */

local Imodman *mm;
local Ilogman *lm;
local Imainloop *ml;


/* functions */


local int write_dirty_values(void *dummy)
{
	Link *l1, *l2;
	ConfigHandle ch;
	FILE *fp;

	pthread_mutex_lock(&cfgmtx);
	for (l1 = LLGetHead(&files); l1; l1 = l1->next)
	{
		ch = l1->data;
		pthread_mutex_lock(&ch->mutex);
		l2 = LLGetHead(&ch->dirty);
		if (l2)
		{
			if (lm)
				lm->Log(L_INFO, "<config> Writing dirty settings to '%s'",
						ch->filename);

			if ((fp = fopen(ch->filename, "a")))
			{
				struct stat st;
				for (; l2; l2 = l2->next)
				{
					struct Entry *e = l2->data;
					if (e->info) fprintf(fp, "; %s\n", e->info);
					fprintf(fp, "%s = %s\n\n", e->keystr, e->val);
					afree(e->keystr);
					afree(e->info);
					afree(e);
				}
				/* set lastmod so that we don't think it's dirty */
				if (fstat(fileno(fp), &st) == 0)
					ch->lastmod = st.st_mtime;
				fclose(fp);
			}
			else
				if (lm)
					lm->Log(L_WARN, "<config> Failed to write dirty values to '%s'",
							ch->filename);

			LLEmpty(&ch->dirty);

			/* call changed callback */
			if (ch->changed)
				ch->changed(ch->clos);
		}
		pthread_mutex_unlock(&ch->mutex);
	}
	pthread_mutex_unlock(&cfgmtx);

	return TRUE;
}

local void FlushDirtyValues(void)
{
	write_dirty_values(NULL);
}


local void ReportError(const char *error)
{
	if (lm)
		lm->Log(L_WARN, "<config> %s", error);
	else
		fprintf(stderr, "W <config> %s\n", error);
}

local int LocateConfigFile(char *dest, int destlen, const char *arena, const char *name)
{
	const char *path = CFG_CONFIG_SEARCH_PATH;
	struct replace_table repls[] =
		{ { 'n', name }, { 'a', arena } };

	if (!name)
		repls[0].with = arena ? "arena.conf" : "global.conf";

	/* no changing config search path without recompiling
	if (global)
		path = GetStr(global, "General", "ConfigSearchPath");
	*/

	return find_file_on_path(dest, destlen, path, repls, arena ? 2 : 1);
}


#define LINESIZE CFG_MAX_LINE

local void do_load(ConfigHandle ch, const char *arena, const char *name)
{
	APPContext *ctx;
	char line[LINESIZE], *buf, *t;
	char key[MAXSECTIONLEN+MAXKEYLEN+3], *thespot = NULL;

	ctx = InitContext(LocateConfigFile, ReportError, arena);

	/* set up some values */
	AddDef(ctx, "VERSION", ASSSVERSION);
	AddDef(ctx, "BUILDDATE", BUILDDATE);

	/* always prepend this */
	AddFile(ctx, "conf/defs.h");

	/* the actual file */
	AddFile(ctx, name);

	while (GetLine(ctx, line, LINESIZE))
	{
		buf = line;
		/* kill leading spaces */
		while (*buf && (*buf == ' ' || *buf == '\t')) buf++;
		/* kill trailing spaces */
		t = buf + strlen(buf) - 1;
		while (t >= buf && (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')) t--;
		*++t = 0;

		if (*buf == '[' || *buf == '{')
		{
			/* new section: copy to key name */
			/* skip leading brackets/spaces */
			while (*buf == '[' || *buf == '{' || *buf == ' ' || *buf == '\t') buf++;
			/* get rid of training spaces or brackets */
			t = buf + strlen(buf) - 1;
			while (*t == ']' || *t == '}' || *t == ' ' || *t == '\t') *t-- = 0;
			/* copy section name into key */
			strncpy(key, buf, MAXSECTIONLEN);
			strcat(key, ":");
			thespot = key + strlen(key);
		}
		else
		{
			t = strchr(buf, '=');
			if (t)
			{
				char *t2 = t + 1, *data;

				/* kill = sign and spaces before it */
				while (*t == ' ' || *t == '=' || *t == '\t') *t-- = 0;
				/* kill spaces before value */
				while (*t2 == ' ' || *t2 == '=' || *t2 == '\t') t2++;

				data = SCAdd(ch->thestrings, t2);
				
				if (strchr(buf, ':'))
				{
					/* this syntax lets you specify a section and key on
					 * one line. it does _not_ modify the "current
					 * section" */
					HashReplace(ch->thetable, buf, data);
				}
				else
				{
					astrncpy(thespot, buf, MAXKEYLEN); /* modifies key */
					HashReplace(ch->thetable, key, data);
				}
			}
			else
			{
				/* there is no value for this key, so enter it with the
				 * empty string. */
				astrncpy(thespot, buf, MAXKEYLEN);
				HashReplace(ch->thetable, key, "");
			}
		}
	}

	FreeContext(ctx);
}


local void ReloadConfigFile(ConfigHandle ch)
{
	struct stat st;

	pthread_mutex_lock(&ch->mutex);

	/* just in case */
	write_dirty_values(NULL);

	/* free this stuff, then create it again */
	SCFree(ch->thestrings);
	HashFree(ch->thetable);
	ch->thetable = HashAlloc(383);
	ch->thestrings = SCAlloc();

	/* now load file again */
	do_load(ch, ch->arena, ch->name);

	ch->lastmod = stat(ch->filename, &st) == 0 ? st.st_mtime : 0;

	/* call changed callback */
	if (ch->changed)
		ch->changed(ch->clos);

	pthread_mutex_unlock(&ch->mutex);
}


local int check_modified_files(void *dummy)
{
	Link *l;
	ConfigHandle ch;
	struct stat st;

	pthread_mutex_lock(&cfgmtx);
	for (l = LLGetHead(&files); l; l = l->next)
	{
		ch = l->data;
		if (stat(ch->filename, &st) == 0)
			if (st.st_mtime != ch->lastmod)
			{
				if (lm)
					lm->Log(L_INFO, "<config> Reloading modified file from disk '%s'",
							ch->filename);
				ReloadConfigFile(ch);
				/* call changed callback */
				if (ch->changed)
					ch->changed(ch->clos);
			}
	}
	pthread_mutex_unlock(&cfgmtx);

	return TRUE;
}

local void CheckModifiedFiles(void)
{
	check_modified_files(NULL);
}


local ConfigHandle new_file()
{
	ConfigHandle f;
	pthread_mutexattr_t attr;

	f = amalloc(sizeof(struct ConfigFile));
	f->refcount = 1;
	f->thetable = HashAlloc(383);
	f->thestrings = SCAlloc();
	f->changed = NULL;
	LLInit(&f->dirty);

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&f->mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	return f;
}


local ConfigHandle LoadConfigFile(const char *arena, const char *name,
		ConfigChangedFunc func, void *clos)
{
	ConfigHandle thefile;
	char fname[PATH_MAX];
	struct stat st;

	/* make sure at least the base file exists */
	if (LocateConfigFile(fname, PATH_MAX, arena, name) == -1)
		return NULL;

	/* first try to get it out of the table */
	thefile = HashGetOne(opened, fname);
	if (thefile)
	{
		thefile->refcount++;
		return thefile;
	}

	/* if not, make a new one */
	thefile = new_file();
	thefile->filename = astrdup(fname);
	thefile->arena = astrdup(arena);
	thefile->name = astrdup(name);
	thefile->lastmod = stat(fname, &st) == 0 ? st.st_mtime : 0;
	thefile->changed = func;
	thefile->clos = clos;

	/* load the settings */
	do_load(thefile, arena, name);

	/* add this to the opened table */
	pthread_mutex_lock(&cfgmtx);
	HashAdd(opened, fname, thefile);
	LLAdd(&files, thefile);
	pthread_mutex_unlock(&cfgmtx);

	return thefile;
}

local void FreeConfigFile(ConfigHandle ch)
{
	if (!ch) return;

	pthread_mutex_lock(&ch->mutex);
	if (--ch->refcount < 1)
	{
		/* don't call callbacks for dirty values that are written as
		 * we're closing the file */
		ch->changed = NULL;
		write_dirty_values(NULL);
		pthread_mutex_unlock(&ch->mutex);

		pthread_mutex_lock(&cfgmtx);
		HashRemove(opened, ch->filename, ch);
		LLRemove(&files, ch);
		pthread_mutex_unlock(&cfgmtx);

		SCFree(ch->thestrings);
		HashFree(ch->thetable);
		pthread_mutex_destroy(&ch->mutex);
		afree(ch->filename);
		afree(ch->arena);
		afree(ch->name);
		afree(ch);
	}
	else
		pthread_mutex_unlock(&ch->mutex);
}


local const char *GetStr(ConfigHandle ch, const char *sec, const char *key)
{
	char keystring[MAXSECTIONLEN+MAXKEYLEN+2];
	const char *res;

	if (!ch) return NULL;
	if (ch == GLOBAL) ch = global;

	pthread_mutex_lock(&ch->mutex);
	if (sec && key)
	{
		snprintf(keystring, MAXSECTIONLEN+MAXKEYLEN+1, "%s:%s", sec, key);
		res = HashGetOne(ch->thetable, keystring);
	}
	else if (sec)
		res = HashGetOne(ch->thetable, sec);
	else if (key)
		res = HashGetOne(ch->thetable, key);
	else
		res = NULL;
	pthread_mutex_unlock(&ch->mutex);

	return res;
}

local int GetInt(ConfigHandle ch, const char *sec, const char *key, int def)
{
	char *next;
	const char *str;
	int ret;

	str = GetStr(ch, sec, key);
	if (!str) return def;
	ret = strtol(str, &next, 0);
	return str != next ? ret : str[0] == 'y' || str[0] == 'Y';
}


local void SetStr(ConfigHandle ch, const char *sec, const char *key, const char *val, const char *info)
{
	struct Entry *e;
	char keystring[MAXSECTIONLEN+MAXKEYLEN+2], *data;

	if (!ch || !val) return;
	if (ch == GLOBAL) ch = global;

	if (sec && key)
		snprintf(keystring, MAXSECTIONLEN+MAXKEYLEN+1, "%s:%s", sec, key);
	else if (sec)
		astrncpy(keystring, sec, MAXSECTIONLEN+MAXKEYLEN+1);
	else if (key)
		astrncpy(keystring, key, MAXSECTIONLEN+MAXKEYLEN+1);
	else
		return;

	/* make a dirty list entry for it */
	e = amalloc(sizeof(*e));
	e->keystr = astrdup(keystring);
	e->info = astrdup(info);

	pthread_mutex_lock(&ch->mutex);
	data = SCAdd(ch->thestrings, val);
	HashReplace(ch->thetable, keystring, data);
	e->val = data;
	LLAdd(&ch->dirty, e);
	pthread_mutex_unlock(&ch->mutex);
}

local void SetInt(ConfigHandle ch, const char *sec, const char *key, int value, const char *info)
{
	char num[16];
	snprintf(num, 16, "%d", value);
	SetStr(ch, sec, key, num, info);
}


local void set_timers()
{
	int dirty, files;

	dirty = GetInt(global, "Config", "FlushDirtyValuesInterval", 500);
	files = GetInt(global, "Config", "CheckModifiedFilesInterval", 18000);

	ml->ClearTimer(write_dirty_values, -1);
	if (dirty)
		ml->SetTimer(write_dirty_values, 700, dirty, NULL, -1);

	ml->ClearTimer(check_modified_files, -1);
	if (files)
		ml->SetTimer(check_modified_files, 10500, files, NULL, -1);
}


local void global_changed(void *dummy)
{
	DO_CBS(CB_GLOBALCONFIGCHANGED, ALLARENAS, GlobalConfigChangedFunc, ());
	set_timers(); /* in case these changed */
}



/* interface */

local Iconfig _int =
{
	INTERFACE_HEAD_INIT(I_CONFIG, "config-file")
	GetStr, GetInt, SetStr, SetInt,
	LoadConfigFile, FreeConfigFile, ReloadConfigFile,
	FlushDirtyValues, CheckModifiedFiles
};


EXPORT int MM_config(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;

		LLInit(&files);
		opened = HashAlloc(23);

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&cfgmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		global = LoadConfigFile(NULL, NULL, global_changed, NULL);
		if (!global) return MM_FAIL;
	
		lm = NULL;

		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!ml) return MM_FAIL;

		set_timers();

		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(lm);
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(write_dirty_values, -1);
		mm->ReleaseInterface(ml);

		/* one last time... */
		write_dirty_values(NULL);

		{
			/* try to clear existing files */
			Link *l, *n;
			for (l = LLGetHead(&files); l; l = n)
			{
				n = l->next;
				FreeConfigFile(l->data);
			}
		}

		HashFree(opened);
		pthread_mutex_destroy(&cfgmtx);

		return MM_OK;
	}
	return MM_FAIL;
}

