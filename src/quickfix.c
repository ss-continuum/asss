
/* dist: public */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef WIN32
#include <io.h> /* mktemp */
#endif

#include "asss.h"
#include "cfghelp.h"
#include "filetrans.h"


local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Ilogman *lm;
local Icfghelp *cfghelp;
local Ifiletrans *filetrans;
local Inet *net;


#ifdef NEED_STRCASESTR
/* taken from
 * http://www2.ics.hawaii.edu/~esb/2001fall.ics451/strcasestr.html */
local const char * strcasestr(const char* haystack, const char* needle)
{
	int i;
	int nlength = strlen (needle);
	int hlength = strlen (haystack);

	if (nlength > hlength) return NULL;
	if (hlength <= 0) return NULL;
	if (nlength <= 0) return haystack;
	/* hlength and nlength > 0, nlength <= hlength */
	for (i = 0; i <= (hlength - nlength); i++)
		if (strncasecmp (haystack + i, needle, nlength) == 0)
			return haystack + i;
	/* substring not found */
	return NULL;
}
#endif


local void try_section(const char *limit, const struct section_help *sh,
		ConfigHandle ch, FILE *f, const char *secname)
{
	int secgood, keygood, j;
	const struct key_help *kh;
	char min[16];
	const char *max;

	secgood = !limit || strcasestr(secname, limit) != NULL;
	for (j = 0; j < sh->keycount; j++)
	{
		kh = &sh->keys[j];
		keygood = !limit || strcasestr(kh->name, limit) != NULL;
		if ((secgood || keygood) && strcmp(kh->loc, "Arena") == 0)
		{
			const char *val = cfg->GetStr(ch, secname, kh->name);
			if (val == NULL)
				val = "<unset>";
			if (kh->range)
			{
				max = delimcpy(min, kh->range, 16, '-');
				if (max == NULL) max = "";
			}
			else
			{
				min[0] = 0;
				max = "";
			}
			/* sec:key:val:min:max:help */
			fprintf(f, "%s:%s:%s:%s:%s:%s\r\n",
					secname, kh->name,
					val, min, max, kh->helptext);
		}
	}
}

local void do_quickfix(int pid, const char *limit)
{
	int i, fd, arena;
	ConfigHandle ch;
	const struct section_help *sh;
	char name[] = "tmp/quickfix-XXXXXX";
	FILE *f;

	arena = pd->players[pid].arena;
	if (ARENA_BAD(arena)) return;
	ch = aman->arenas[arena].cfg;

#ifndef WIN32
	fd = mkstemp(name);

	if (fd == -1)
	{
		lm->Log(L_WARN, "<quickfix> Can't create temp file. Make sure tmp/ exists.");
		chat->SendMessage(pid, "Error: can't create temporary file.");
		return;
	}

	f = fdopen(fd, "wb");
#else
	if (!mktemp(name))
	{
		lm->Log(L_WARN, "<quickfix> Can't create temp file. Make sure tmp/ exists.");
		chat->SendMessage(pid, "Error: can't create temporary file.");
		return;
	}

	f = fopen(name, "wb");
#endif

	/* construct server.set */
	for (i = 0; i < cfghelp->section_count; i++)
	{
		sh = &cfghelp->sections[i];
		if (strcmp(sh->name, "All"))
			try_section(limit, sh, ch, f, sh->name);
		else
		{
			try_section(limit, sh, ch, f, "Warbird");
			try_section(limit, sh, ch, f, "Javelin");
			try_section(limit, sh, ch, f, "Spider");
			try_section(limit, sh, ch, f, "Leviathan");
			try_section(limit, sh, ch, f, "Terrier");
			try_section(limit, sh, ch, f, "Weasel");
			try_section(limit, sh, ch, f, "Lancaster");
			try_section(limit, sh, ch, f, "Shark");
		}
	}

	fclose(f);

	/* send and delete file */
	chat->SendMessage(pid, "Sending settings...");
	filetrans->SendFile(pid, name, "server.set", TRUE);
}


local helptext_t quickfix_help =
"Module: quickfix\n"
"Targets: none\n"
"Args: <limiting text>\n"
"Lets you quickly change arena settings. This will display some list of\n"
"settings with their current values and allow you to change them. The\n"
"argument to this command can be used to limit the list of settings\n"
"displayed.\n";

local void Cquickfix(const char *params, int pid, const Target *target)
{
	do_quickfix(pid, params[0] ? params : NULL);
}


local void p_settingchange(int pid, byte *pkt, int len)
{
	int arena;
	ConfigHandle ch;
	time_t tm = time(NULL);
	const char *p = (const char*)pkt + 1;
	char sec[MAXSECTIONLEN], key[MAXKEYLEN], info[128];

	arena = pd->players[pid].arena;
	if (ARENA_BAD(arena)) return;
	ch = aman->arenas[arena].cfg;

#define CHECK(n) \
	if (!n) { \
		lm->LogP(L_MALICIOUS, "quickfix", pid, \
				"Badly formatted setting change"); \
		return; \
	}

	snprintf(info, 100, "set by %s with ?quickfix on ", pd->players[pid].name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	while ((p-(char*)pkt) < len)
	{
		printf("quickfix: setting: '%s'\n", p);
		p = delimcpy(sec, p, MAXSECTIONLEN, ':');
		CHECK(p)
		p = delimcpy(key, p, MAXKEYLEN, ':');
		CHECK(p)
		lm->LogP(L_INFO, "quickfix", pid, "Setting %s:%s = %s",
				sec, key, p);
		cfg->SetStr(ch, sec, key, p, info);
		p = p + strlen(p) + 1;
		if (p[0] == '\0') break;
	}
}


EXPORT int MM_quickfix(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfghelp = mm->GetInterface(I_CFGHELP, ALLARENAS);
		filetrans = mm->GetInterface(I_FILETRANS, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		if (!pd || !aman || !cfg || !chat || !cmd || !lm ||
				!cfghelp || !filetrans || !net)
			return MM_FAIL;

		net->AddPacket(C2S_SETTINGCHANGE, p_settingchange);
		cmd->AddCommand("quickfix", Cquickfix, quickfix_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_SETTINGCHANGE, p_settingchange);
		cmd->RemoveCommand("quickfix", Cquickfix);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfghelp);
		mm->ReleaseInterface(filetrans);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}

