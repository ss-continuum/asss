
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>


#include "asss.h"

#include "pathutil.h"


/* structs */

struct ConfigFile
{
	int refcount;
	HashTable *thetable;
	StringChunk *thestrings;
	char id[64];
};


/* function prototypes */

local const char *GetStr(ConfigHandle, const char *, const char *);
local int GetInt(ConfigHandle, const char *, const char *, int);

local ConfigHandle LoadConfigFile(const char *arena, const char *name);
local void FreeConfigFile(ConfigHandle);
local int LocateConfigFile(char *dest, int destlen, const char *arena, const char *name);


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


/* functions */

EXPORT int MM_config(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
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
		return MM_OK;
	}
	return MM_FAIL;
}

#define LINESIZE 512

local int ProcessConfigFile(
		HashTable *thetable,
		StringChunk *thestrings,
		HashTable *defines,
		const char *arena,
		const char *name,
		char *initsection)
{
	FILE *f;
	char realbuf[LINESIZE], *buf, *t, *t2;
	char key[MAXNAMELEN+MAXKEYLEN+3], *thespot = NULL, *data;

	if (LocateConfigFile(realbuf, PATH_MAX, arena, name) == -1)
		return MM_FAIL;
	f = fopen(realbuf, "r");
	if (!f) return MM_FAIL;

	if (initsection)
	{
		astrncpy(key, initsection, MAXNAMELEN+3);
		thespot = strchr(key, ':') + 1;
	}

	while (buf = realbuf, fgets(buf, LINESIZE, f))
	{
		/* kill leading spaces */
		while (*buf && (*buf == ' ' || *buf == '\t')) buf++;
		/* kill trailing spaces */
		t = buf + strlen(buf) - 1;
		while (t >= buf && (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')) t--;
		*++t = 0;

		if (*buf == '[' || *buf == '{')
		{	/* new section: copy to key name */
			/* skip leading brackets/spaces */
			while (*buf == '[' || *buf == '{' || *buf == ' ' || *buf == '\t') buf++;
			/* get rid of training spaces or brackets */
			t = buf + strlen(buf) - 1;
			while (*t == ']' || *t == '}' || *t == ' ' || *t == '\t') t--;
			*++t = 0;
			/* copy section name into key */
			snprintf(key, MAXNAMELEN, "%s:", buf);
			thespot = key + strlen(key);
		}
		else if (*buf == '#')
		{
			/* skip '#' */
			buf++;
			/* strip trailing spaces */
			t = buf + strlen(buf) - 1;
			while (*t == ' ' || *t == '\t' || *t == '"') t--;
			*++t = 0;
			/* process */
			if (!strncmp(buf, "include", 7))
			{
				buf += 7;
				while (*buf == ' ' || *buf == '\t' || *buf == '"') buf++;
				/* recur with name equal to the argument to #include.
				 * because of the search path, this will allow absolute
				 * filenames as name to be found. */
				ProcessConfigFile(thetable, thestrings, defines, arena, buf, key);
			}
			else if (!strncmp(buf, "define", 6))
			{
				buf += 6;
				while (*buf == ' ' || *buf == '\t') buf++;
				/* find the space */
				t = strchr(buf, ' ');
				if (!t) t = strchr(buf, '\t');
				if (t)
				{
					/* kill it */
					*t++ = 0;
					while (*t && *t == ' ') t++;
					HashReplace(defines, buf, astrdup(t));
				}
				else
				{	/* empty define */
					HashReplace(defines, buf, astrdup(""));
				}
			}
#if 0       /* this stuff will take a bit of work */
			else if (!strncmp(buf, "ifdef", 5))
			{
				buf += 5;
				while (*buf == ' ' || *buf == '\t') buf++;
				/* ... */
			}
			if (!strncmp(buf, "endif", 5))
			{
				/* do nothing */
			}
#endif
		}
		else if (thespot && !(*buf == '/' || *buf == ';' || *buf == '}' || *buf == 0))
		{
			t = strchr(buf, '=');
			if (!t) t = strchr(buf, ':');
			if (t)
			{
				char *trydef;

				t2 = t + 1;
				/* kill = sign and spaces before it */
				while (*t == ' ' || *t == '=' || *t == '\t' || *t == ':') t--;
				*++t = 0;
				/* kill spaces before value */
				while (*t2 == ' ' || *t2 == '=' || *t2 == '\t' || *t2 == ':') t2++;

				astrncpy(thespot, buf, MAXKEYLEN); /* modifies key */

				/* process #defines */
				trydef = HashGetOne(defines, t2);
				if (trydef) t2 = trydef;

				data = SCAdd(thestrings, t2);
				HashReplace(thetable, key, data);
			}
			else
			{
				/* there is no value for this key, so enter it with the
				 * empty string. */
				astrncpy(thespot, buf, MAXKEYLEN);
				HashReplace(thetable, key, "");
			}
		}
	}
	fclose(f);
	return MM_OK;
}


local void afree_hash(char *key, void *val, void *d)
{
	afree(val);
}

ConfigHandle LoadConfigFile(const char *arena, const char *name)
{
	ConfigHandle thefile;
	HashTable *defines;
	char id[64];

	/* first try to get it out of the table */
	snprintf(id, 64, "%s:%s", arena ? arena : "", name ? name : "");
	thefile = HashGetOne(opened, ToLowerStr(id));
	if (thefile)
	{
		thefile->refcount++;
		return thefile;
	}

	/* ok, it's not there. open it. */
	thefile = amalloc(sizeof(struct ConfigFile));
	thefile->refcount = 1;
	thefile->thetable = HashAlloc(383);
	thefile->thestrings = SCAlloc();
	defines = HashAlloc(17);

	/*printf("config: LoadConfigFile(%s, %s)\n", arena, name);*/

	if (ProcessConfigFile(thefile->thetable, thefile->thestrings, defines, arena, name, NULL) == MM_OK)
	{
		files++;
		HashEnum(defines, afree_hash, NULL);
		HashFree(defines);

		/* add this to the opened table */
		astrncpy(thefile->id, id, 64);
		HashAdd(opened, id, thefile);

		return thefile;
	}
	else
	{
		HashEnum(defines, afree_hash, NULL);
		HashFree(defines);
		HashFree(thefile->thetable);
		SCFree(thefile->thestrings);
		afree(thefile);
		return NULL;
	}
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



int LocateConfigFile(char *dest, int destlen, const char *arena, const char *name)
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




