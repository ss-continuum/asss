
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "asss.h"



/* prototypes */

local void SendMessage(int, char *, ...);
local void PChat(int, byte *, int);


/* global data */

local Inet *net;
local Iconfig *cfg;
local Ilogman *log;
local Icmdman *cmd;
local Imodman *mm;

local PlayerData *players;
local ArenaData **arenas;

local Ichat _int =
{
	SendMessage
};

local int cfg_msgrel;




int MM_chat(int action, Imodman *mm2)
{
	if (action == MM_LOAD)
	{
		Icore *core;

		mm = mm2;
		net = mm->GetInterface(I_NET);
		cfg = mm->GetInterface(I_CONFIG);
		log = mm->GetInterface(I_LOGMAN);
		core = mm->GetInterface(I_CORE);
		cmd = mm->GetInterface(I_CMDMAN);

		if (!net || !cfg || !log || !core) return MM_FAIL;

		players = mm->players;
		arenas = core->arenas;

		cfg_msgrel = cfg->GetInt(GLOBAL, "Chat", "MessageReliable", 1);
		net->AddPacket(C2S_CHAT, PChat);
		mm->RegisterInterface(I_CHAT, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_int);
		net->RemovePacket(C2S_CHAT, PChat);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "chat - handles all chat packets and passes commands on to cmdman";
	}
	return MM_OK;
}


#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define MOD_CHAT_CHAR '\\'

void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p, *to;
	int setc = 0, i, arena, freq;
	static int set[MAXPLAYERS];

	to = amalloc(len + 30);
	to->pktype = S2C_CHAT;
	to->type = from->type;
	to->pid = pid;
	strcpy(to->text, from->text);
	freq = players[pid].freq;
	arena = players[pid].arena;
	if (arena < 0) return;

	switch (from->type)
	{
		case MSG_ARENA:
			log->Log(LOG_BADDATA,"Recieved arena message (%s) (%s)",
				arenas[arena]->name, players[pid].name);
			break;

		case MSG_PUBMACRO:
			/* fall through */

		case MSG_PUB:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
				cmd = mm->GetInterface(I_CMDMAN);
/*				log->Log(LOG_DEBUG,"Command (%s:<arena>) %s", */
/*						players[pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, TARGET_ARENA);
			}
			else if (from->text[0] == MOD_CHAT_CHAR)
			{
				log->Log(LOG_DEBUG,"Mod chat: (%s) %s> %s",
					arenas[arena]->name, players[pid].name, from->text+1);
				to->type = MSG_SYSOPWARNING;
				sprintf(to->text, "%s> %s", players[pid].name, from->text+1);
				for (i = 0; i < MAXPLAYERS; i++)
					if (	net->GetStatus(i) == S_CONNECTED &&
							players[i].oplevel > 0 &&
							i != pid)
						set[setc++] = i;
				set[setc] = -1;
				net->SendToSet(set, (byte*)to, len+30, NET_RELIABLE);
				/* ugliness: sending garbage after null terminator */
			}
			else /* normal pub message */
			{
				log->Log(LOG_DEBUG,"Pub message: (%s) %s> %s",
					arenas[arena]->name, players[pid].name, from->text);
				net->SendToArena(arena, pid, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_NMEFREQ:
			freq = players[from->pid].freq;
			/* fall through */

		case MSG_FREQ:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
				cmd = mm->GetInterface(I_CMDMAN);
/*				log->Log(LOG_DEBUG,"Command (%s:<freq>) %s", */
/*						players[pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, TARGET_FREQ);
			}
			else
			{
				log->Log(LOG_DEBUG,"Freq message: (%s:%i) %s> %s",
					arenas[arena]->name, freq, players[pid].name, from->text);
				for (i = 0; i < MAXPLAYERS; i++)
					if (	players[i].freq == freq &&
							players[i].arena == arena &&
							i != pid)
						set[setc++] = i;
				set[setc] = -1;
				net->SendToSet(set, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_PRIV:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
				cmd = mm->GetInterface(I_CMDMAN);
/*				log->Log(LOG_DEBUG,"Command (%s:%s) %s", */
/*						players[pid].name, players[from->pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, from->pid);
			}
			else
			{
				log->Log(LOG_DEBUG,"Priv message: (%s) %s:%s> %s",
					arenas[arena]->name, players[pid].name,
					players[from->pid].name, from->text);
				net->SendToOne(from->pid, (byte*)to, len, cfg_msgrel);
			}
			break;
				
		case MSG_INTERARENAPRIV:
			/* FIXME */
			break;

		case MSG_SYSOPWARNING:
			log->Log(LOG_BADDATA,"Recieved sysop message (%s) (%s)",
					arenas[arena]->name, players[pid].name);
			break;

		case MSG_CHAT:
			log->Log(LOG_DEBUG,"Chat message: (%s) %s> %s",
				arenas[arena]->name, players[pid].name, from->text);
			/* FIXME */
			break;
	}

	free(to);
}


void SendMessage(int pid, char *str, ...)
{
	int size;
	struct ChatPacket *cp = amalloc(256);
	va_list args;
	
	va_start(args, str);
	size = vsnprintf(cp->text, 250, str, args) + 6;
	va_end(args);

	cp->pktype = S2C_CHAT;
	cp->type = MSG_ARENA;
	net->SendToOne(pid, (byte*)cp, size, NET_RELIABLE);
	free(cp);
}


