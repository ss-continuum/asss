
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>


#include "asss.h"

#include "pathutil.h"

#include "app.h"


#define IDLEN 128

/* structs */

struct ConfigFile
{
	int refcount;
	HashTable *thetable;
	StringChunk *thestrings;
	pthread_mutex_t mutex;
	char id[IDLEN];
};


/* function prototypes */

local const char *GetStr(ConfigHandle, const char *, const char *);
local int GetInt(ConfigHandle, const char *, const char *, int);

local ConfigHandle LoadConfigFile(const char *arena, const char *name);
local void FreeConfigFile(ConfigHandle);


/* globals */

local Iconfig _int =
{
	INTERFACE_HEAD_INIT(I_CONFIG, "config-file")
	GetStr, GetInt, /*SetConfigStr, SetConfigInt, */
	LoadConfigFile, FreeConfigFile
};

local ConfigHandle global;
local HashTable *opened;
local int files = 0;

local Imodman *mm;
local Ilogman *lm;


/* functions */

EXPORT int MM_config(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		files = 0;
		opened = HashAlloc(23);
		global = LoadConfigFile(NULL, NULL);
		if (!global) return MM_FAIL;
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		FreeConfigFile(global);
		HashFree(opened);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


local void ReportError(const char *error)
{
	if (lm)
		lm->Log(L_WARN, "<config> %s", error);
}

local int LocateConfigFile(char *dest, int destlen, const char *arena, const char *name)
{
	const char *path = NULL;
	struct replace_table repls[] =
		{ { 'n', name }, { 'a', arena } };

	if (!name)
		repls[0].with = arena ? "arena.conf" : "global.conf";

	if (global)
		path = GetStr(global, "General", "ConfigSearchPath");
	if (!path)
		path = DEFAULTCONFIGSEARCHPATH;

	return find_file_on_path(dest, destlen, path, repls, arena ? 2 : 1);
}

#define LINESIZE 1024

local ConfigHandle load_file(const char *arena, const char *name)
{
	ConfigHandle f;
	char line[LINESIZE], *buf, *t;
	char key[MAXNAMELEN+MAXKEYLEN+3], *thespot = NULL;
	APPContext *ctx;

	f = amalloc(sizeof(struct ConfigFile));
	f->refcount = 1;
	f->thetable = HashAlloc(383);
	f->thestrings = SCAlloc();

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
			strncpy(key, buf, MAXNAMELEN);
			strcat(key, ":");
			thespot = key + strlen(key);
		}
		else
		{
			t = strchr(buf, '=');
			if (!t) t = strchr(buf, ':');
			if (t)
			{
				char *t2 = t + 1, *data;

				/* kill = sign and spaces before it */
				while (*t == ' ' || *t == '=' || *t == '\t' || *t == ':') *t-- = 0;
				/* kill spaces before value */
				while (*t2 == ' ' || *t2 == '=' || *t2 == '\t' || *t2 == ':') t2++;

				astrncpy(thespot, buf, MAXKEYLEN); /* modifies key */

				data = SCAdd(f->thestrings, t2);
				HashReplace(f->thetable, key, data);
			}
			else
			{
				/* there is no value for this key, so enter it with the
				 * empty string. */
				astrncpy(thespot, buf, MAXKEYLEN);
				HashReplace(f->thetable, key, "");
			}
		}
	}

	FreeContext(ctx);

	return f;
}


ConfigHandle LoadConfigFile(const char *arena, const char *name)
{
	ConfigHandle thefile;
	char id[IDLEN];

	/* first try to get it out of the table */
	snprintf(id, IDLEN, "%s:%s", arena ? arena : "", name ? name : "");
	thefile = HashGetOne(opened, ToLowerStr(id));
	if (thefile)
	{
		thefile->refcount++;
		return thefile;
	}

	/* ok, it's not there. open it. */
	thefile = load_file(arena, name);

	/* add this to the opened table */
	astrncpy(thefile->id, id, IDLEN);
	HashAdd(opened, id, thefile);
	files++;

	return thefile;
}

void FreeConfigFile(ConfigHandle ch)
{
	if (ch && --ch->refcount < 1)
	{
		SCFree(ch->thestrings);
		HashFree(ch->thetable);
		HashRemove(opened, ch->id, ch);
		afree(ch);
		files--;
	}
}


int GetInt(ConfigHandle ch, const char *sec, const char *key, int def)
{
	const char *res = GetStr(ch, sec, key);
	return res ? strtol(res, NULL, 0) : def;
}


const char *GetStr(ConfigHandle ch, const char *sec, const char *key)
{
	char keystring[MAXNAMELEN+MAXKEYLEN+2];

	if (!ch)
		return NULL;
	if (ch == GLOBAL) ch = global;

	if (sec && key)
	{
		snprintf(keystring, MAXNAMELEN+MAXKEYLEN+1, "%s:%s", sec, key);
		return HashGetOne(ch->thetable, keystring);
	}
	else if (sec)
		return HashGetOne(ch->thetable, sec);
	else if (key)
		return HashGetOne(ch->thetable, key);
	else
		return NULL;
}

