
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>


#include "asss.h"



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

static int ProcessConfigFile(HashTable *thetable, HashTable *defines, const char *name)
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
		while (*buf && (*buf == ' ' || *buf == '\t')) buf++; /* kill leading spaces */
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
			/* skip '#' */
			buf++;
			/* strip trailing spaces */
			t = buf + strlen(buf) - 1;
			while (*t == ' ' || *t == '\t') t--;
			*++t = 0;
			/* process */
			if (!strncmp(buf, "include", 7))
			{
				buf += 7;
				while (*buf == ' ' || *buf == '\t') buf++;
				if (ProcessConfigFile(thetable, defines, buf) == MM_FAIL)
				{
					if (log) log->Log(LOG_ERROR, "Cannot find #included file '%s'", buf);
					/* return MM_FAIL; let's not abort on #include error */ 
				}
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
					HashAdd(defines, buf, astrdup(t));
				}
				else
				{	/* empty define */
					HashAdd(defines, buf, astrdup(""));
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
			else
			{
				if (log) log->Log(LOG_ERROR, "Unexpected configuration directive '%s'", buf);
			}
		}
		else if (thespot && !(*buf == '/' || *buf == ';' || *buf == '}'))
		{
			t = strchr(buf, '=');
			if (t)
			{
				char *trydef;

				t2 = t + 1;
				/* kill = sign and spaces before it */
				while (*t == ' ' || *t == '=' || *t == '\t') t--;
				*++t = 0;
				/* kill spaces before value */
				while (*t2 == ' ' || *t2 == '=' || *t2 == '\t') t2++;

				astrncpy(thespot, buf, MAXKEYLEN); /* modifies key */

				/* process #defines */
				trydef = HashGetOne(defines, t2);
				if (trydef) t2 = trydef;

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
	HashTable *defines = HashAlloc(17);
	if (ProcessConfigFile(thetable, defines, name) == MM_OK)
	{
		files++;
		HashEnum(defines, afree);
		HashFree(defines);
		return (ConfigHandle)thetable;
	}
	else
	{
		HashEnum(defines, afree);
		HashFree(defines);
		HashFree(thetable);
		return NULL;
	}
}

void FreeConfigFile(ConfigHandle ch)
{
	HashTable *thetable = (HashTable*)ch;
	HashEnum(thetable, afree);
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
	HashTable *thetable = (HashTable*)ch;

	if (!thetable) thetable = (HashTable*)global;

	snprintf(keystring, MAXNAMELEN+MAXKEYLEN+1, "%s:%s", sec, key);

	return HashGetOne(thetable, keystring);
}




