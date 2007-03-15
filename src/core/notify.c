
/* dist: public */

#include "asss.h"

local Ichat *chat;
local Icmdman *cmd;
local Iconfig *cfg;
local Imodman *mm;

#define NOTIFY_COMMANDS_LENGTH 255
local char notify_commands[NOTIFY_COMMANDS_LENGTH];

local helptext_t notify_help =
"Targets: none\n"
"Args: <message>\n"
"Sends the message to all online staff members.\n";

local void Cnotify(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (IS_ALLOWED(chat->GetPlayerChatMask(p), MSG_MODCHAT))
	{
		chat->SendModMessage("%s {%s} %s: %s", tc,
				arena->name, p->name, params);
		chat->SendMessage(p, "Message has been sent to online staff");
	}
}

local void register_commands()
{
	char cmds_str[NOTIFY_COMMANDS_LENGTH];
	char buf[64];
	const char *tmp = NULL;
	const char *cfg_result;
		       
	cfg_result = cfg->GetStr(GLOBAL, "Notify", "AlertCommand");

	//default to ?cheater
	if (!cfg_result) 
		astrncpy(notify_commands, "cheater", NOTIFY_COMMANDS_LENGTH);
	else
		astrncpy(notify_commands, cfg_result, NOTIFY_COMMANDS_LENGTH);
	
	//make a copy, so that strsplit doesn't destory the original
	astrncpy(cmds_str, notify_commands, NOTIFY_COMMANDS_LENGTH);
	while (strsplit(cmds_str, " ,:;", buf, sizeof(buf), &tmp))
	{
		cmd->AddCommand(ToLowerStr(buf), Cnotify, ALLARENAS, notify_help);
	}
}

local void unregister_commands()
{
	char cmds_str[NOTIFY_COMMANDS_LENGTH];
	char buf[64];
	const char *tmp = NULL;
	
	//make a copy, so that strsplit doesn't destory the original
	astrncpy(cmds_str, notify_commands, NOTIFY_COMMANDS_LENGTH);
	while (strsplit(cmds_str, " ,:;", buf, sizeof(buf), &tmp))
	{
		cmd->RemoveCommand(ToLowerStr(buf), Cnotify, ALLARENAS);
	}
}

EXPORT int MM_notify(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!chat || !cmd || !cfg) return MM_FAIL;
	
		register_commands();
		
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		unregister_commands();
		
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	return MM_FAIL;
}




