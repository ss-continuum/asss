
#include <string.h>

#include "asss.h"

local Ichat *chat;
local Icmdman *cmdman;


local void do_cmd_help(int pid, const char *cmd)
{
	char buf[256], *t;
	const char *temp = NULL;
	helptext_t ht;
	
	ht = cmdman->GetHelpText(cmd);

	if (ht)
	{
		chat->SendMessage(pid, "Help on '?%s':", cmd);
		while (strsplit(ht, "\n", buf, 256, &temp))
		{
			for (t = buf; *t; t++)
				if (*t == '{' || *t == '}')
					*t = '\'';
			chat->SendMessage(pid, "  %s", buf);
		}
	}
	else
		chat->SendMessage(pid, "Sorry, I don't know anything about ?%s", cmd);
}


#include "cfghelp.inc"

local void send_msg_cb(const char *line, void *clos)
{
	chat->SendMessage(*(int*)clos, "  %s", line);
}

/* possible fixme: rewrite these two functions to use binary search */
local const struct section_help *find_sec(const char *sec)
{
	int i;
	for (i = 0; i < cfg_help_section_count; i++)
		if (strcasecmp(sec, cfg_help_sections[i].name) == 0)
			return &cfg_help_sections[i];
	return NULL;
}

local const struct key_help *find_key(const struct section_help *sh, const char *key)
{
	int i;
	for (i = 0; i < sh->keycount; i++)
		if (strcasecmp(key, sh->keys[i].name) == 0)
			return &sh->keys[i];
	return NULL;
}

local void do_list_sections(int pid)
{
	chat->SendMessage(pid, "Known config file sections:");
	wrap_text(cfg_help_all_section_names, 100, ' ', send_msg_cb, &pid);
}

local void do_list_keys(int pid, const char *sec)
{
	const struct section_help *sh = find_sec(sec);
	if (sh)
	{
		chat->SendMessage(pid, "Known keys in section %s:", sec);
		wrap_text(sh->all_key_names, 100, ' ', send_msg_cb, &pid);
	}
	else
		chat->SendMessage(pid, "I don't know anything about section %s", sec);
}

local void do_setting_help(int pid, const char *sec, const char *key)
{
	const struct section_help *sh = find_sec(sec);
	if (sh)
	{
		const struct key_help *kh = find_key(sh, key);
		if (kh)
		{
			chat->SendMessage(pid, "Help on setting %s:%s",
					sh->name, kh->name);
			if (kh->mod)
				chat->SendMessage(pid, "  Requires module: %s", kh->mod);
			chat->SendMessage(pid, "  Location: %s", kh->loc);
			chat->SendMessage(pid, "  Type: %s", kh->type);
			if (kh->range)
				chat->SendMessage(pid, "  Range: %s", kh->range);
			if (kh->def)
				chat->SendMessage(pid, "  Default: %s", kh->def);
			wrap_text(kh->helptext, 100, ' ', send_msg_cb, &pid);
		}
		else
			chat->SendMessage(pid, "I don't know anything about key %s", key);
	}
	else
		chat->SendMessage(pid, "I don't know anything about section %s", sec);
}


local helptext_t help_help =
"Targets: none\n"
"Args: <command name> | <setting name (section:key)>\n"
"Displays help on a command or config file setting. Use {?help section:}\n"
"to list known keys in that section. Use {?help :} to list known section\n"
"names.\n";

local void Chelp(const char *params, int pid, const Target *target)
{

	if (params[0] == '?' || params[0] == '*' || params[0] == '!')
		params++;

	if (params[0] == '\0')
		params = "help";

	if (strchr(params, ':'))
	{
		/* setting */
		char secname[MAXSECTIONLEN];
		const char *keyname;

		keyname = delimcpy(secname, params, MAXSECTIONLEN, ':');

		if (secname[0] == '\0')
			do_list_sections(pid);
		else if (keyname[0] == '\0')
			do_list_keys(pid, secname);
		else
			do_setting_help(pid, secname, keyname);
	}
	else
		/* command */
		do_cmd_help(pid, params);
}


EXPORT int MM_help(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
		if (!chat || !cmdman)
			return MM_FAIL;

		cmdman->AddCommand("help", Chelp, help_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmdman->RemoveCommand("help", Chelp);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	return MM_FAIL;
}

