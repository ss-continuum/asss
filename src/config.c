
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>


#include "asss.h"


/* some defines for maximums */

#define MAXNAMELEN 32
#define MAXKEYLEN 32
#define MAXVALUELEN 256


/* function prototypes */

local char *GetStr(ConfigHandle, const char *, const char *);
local int GetInt(ConfigHandle, const char *, const char *, int);

local ConfigHandle LoadConfigFile(const char *);
local void FreeConfigFile(ConfigHandle);


/* globals */


local Iconfig _int =
{
	GetStr, GetInt, /*SetConfigStr, SetConfigInt, */
	LoadConfigFile, FreeConfigFile
};

local ConfigHandle global;
local int files = 0;

local Ilogman *log;


/* functions */

int MM_config(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_LOGMAN, &log);

		files = 0;
		global = LoadConfigFile("global");
		if (!global) global = LoadConfigFile("asss");
		if (!global) global = LoadConfigFile("zone");
		if (!global) return MM_FAIL;
		mm->RegInterface(I_CONFIG, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CONFIG, &_int);
		FreeConfigFile(global);
		/* if (files && (log = mm->GetInterface(I_LOGMAN)))
			printf("Some config files were not freed!"); */
		mm->UnregInterest(I_LOGMAN, &log);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "config - manages configuration information";
	}
	return MM_OK;
}

#define LINESIZE 512

static int ProcessConfigFile(HashTable *thetable, const char *name)
{
	FILE *f;
	char _realbuf[LINESIZE], *buf = _realbuf, *t, *t2;
	char key[MAXNAMELEN+MAXKEYLEN+3], *thespot = NULL, *data;

	sprintf(buf, "conf/%s.conf", name);
	f = fopen(buf, "r");
	if (!f)
	{
		sprintf(buf, "conf/%s", name);
		f = fopen(buf, "r");
	}
	if (!f) return MM_FAIL;

	while (buf = _realbuf, fgets(buf, LINESIZE, f))
	{
		while (*buf == ' ' || *buf == '\t') buf++; /* kill leading spaces */
		RemoveCRLF(buf); /* get rid of newlines */

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
			buf++;
			if (!strncmp(buf, "include", 7))
			{
				buf += 7;
				while (*buf == ' ' || *buf == '\t') buf++;
				if (ProcessConfigFile(thetable, buf) == MM_FAIL)
				{
					if (log) log->Log(LOG_ERROR, "Cannot find #included file: %s", buf);
					/* return MM_FAIL; let's not abort on #include error */ 
				}
			}
			/* add define, ifdef, etc */
		}
		else if (thespot && !(*buf == '/' || *buf == ';' || *buf == '}'))
		{
			t = strchr(buf, '=');
			if (t)
			{
				t2 = t + 1;
				/* kill = sign and spaces before it */
				while (*t == ' ' || *t == '=' || *t == '\t') t--;
				*++t = 0;
				/* kill spaces before value */
				while (*t2 == ' ' || *t2 == '=' || *t2 == '\t') t2++;

				astrncpy(thespot, buf, MAXKEYLEN);
				data = astrdup(t2);
				HashAdd(thetable, key, data);
			}
		}
	}
	fclose(f);
	return MM_OK;
}

ConfigHandle LoadConfigFile(const char *name)
{
	HashTable *thetable = HashAlloc(983);
	if (ProcessConfigFile(thetable, name) == MM_OK)
	{
		files++;
		return (ConfigHandle)thetable;
	}
	else
	{
		HashFree(thetable);
		return NULL;
	}
}

void FreeConfigFile(ConfigHandle ch)
{
	HashTable *thetable = (HashTable*)ch;
	HashEnum(thetable, free);
	HashFree(thetable);
	files--;
}


int GetInt(ConfigHandle ch, const char *sec, const char *key, int def)
{
	char *res = GetStr(ch, sec, key);
	return res ? atoi(res) : def;
}


char *GetStr(ConfigHandle ch, const char *sec, const char *key)
{
	char keystring[MAXNAMELEN+MAXKEYLEN+3];
	LinkedList *res;
	Link *l;
	HashTable *thetable = (HashTable*)ch;

	if (!thetable) thetable = (HashTable*)global;

	snprintf(keystring, MAXNAMELEN+MAXKEYLEN+1, "%s:%s", sec, key);

	res = HashGet(thetable, keystring);

	if ((l = LLGetHead(res)))
		return (char*) l->data;
	else
		return NULL;
}




