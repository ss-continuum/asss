
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "asss.h"



/* prototypes */

local void SendMessage(int, char *, ...);
local void PChat(int, byte *, int);


/* global data */

local Iplayerdata *pd;
local Inet *net;
local Iconfig *cfg;
local Ilogman *log;
local Icmdman *cmd;
local Imodman *mm;
local Iarenaman *aman;

local PlayerData *players;
local ArenaData *arenas;

local int cfg_msgrel;


local Ichat _int = { SendMessage };


int MM_chat(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{

		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_CMDMAN, &cmd);

		if (!net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->data;

		cfg_msgrel = cfg->GetInt(GLOBAL, "Chat", "MessageReliable", 1);
		net->AddPacket(C2S_CHAT, PChat);
		mm->RegInterface(I_CHAT, &_int);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CHAT, &_int);
		net->RemovePacket(C2S_CHAT, PChat);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CMDMAN, &cmd);
		return MM_OK;
	}
	return MM_FAIL;
}


#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define MOD_CHAT_CHAR '\\'

void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p, *to;
	int setc = 0, i, arena, freq;
	static int set[MAXPLAYERS];

	freq = players[pid].freq;
	arena = players[pid].arena;
	if (arena < 0) return;

	to = alloca(len + 30);
	to->pktype = S2C_CHAT;
	to->type = from->type;
	to->sound = 0;
	to->pid = pid;
	strcpy(to->text, from->text);

	switch (from->type)
	{
		case MSG_ARENA:
			log->Log(LOG_BADDATA,"Recieved arena message (%s) (%s)",
				arenas[arena].name, players[pid].name);
			break;

		case MSG_PUBMACRO:
			/* fall through */

		case MSG_PUB:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
/*				log->Log(LOG_DEBUG,"Command (%s:<arena>) %s", */
/*						players[pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, TARGET_ARENA);
			}
			else if (from->text[0] == MOD_CHAT_CHAR)
			{
				log->Log(LOG_DEBUG,"Mod chat: (%s) %s> %s",
					arenas[arena].name, players[pid].name, from->text+1);
				to->type = MSG_SYSOPWARNING;
				sprintf(to->text, "%s> %s", players[pid].name, from->text+1);
				pd->LockStatus();
				for (i = 0; i < MAXPLAYERS; i++)
					if (    players[i].status == S_PLAYING
					     && players[i].oplevel > 0
					     && i != pid)
						set[setc++] = i;
				pd->UnlockStatus();
				set[setc] = -1;
				net->SendToSet(set, (byte*)to, len+30, NET_RELIABLE);
				/* FIXME: sending garbage after null terminator */
			}
			else /* normal pub message */
			{
				log->Log(LOG_DEBUG,"Pub message: (%s) %s> %s",
					arenas[arena].name, players[pid].name, from->text);
				net->SendToArena(arena, pid, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_NMEFREQ:
			freq = players[from->pid].freq;
			/* fall through */

		case MSG_FREQ:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
/*				log->Log(LOG_DEBUG,"Command (%s:<freq>) %s", */
/*						players[pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, TARGET_FREQ);
			}
			else
			{
				log->Log(LOG_DEBUG,"Freq message: (%s:%i) %s> %s",
					arenas[arena].name, freq, players[pid].name, from->text);
				pd->LockStatus();
				for (i = 0; i < MAXPLAYERS; i++)
					if (	players[i].freq == freq &&
							players[i].arena == arena &&
							i != pid)
						set[setc++] = i;
				pd->UnlockStatus();
				set[setc] = -1;
				net->SendToSet(set, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_PRIV:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
			{
/*				log->Log(LOG_DEBUG,"Command (%s:%s) %s", */
/*						players[pid].name, players[from->pid].name, from->text+1); */
				if (cmd) cmd->Command(from->text+1, pid, from->pid);
			}
			else
			{
				log->Log(LOG_DEBUG,"Priv message: (%s) %s:%s> %s",
					arenas[arena].name, players[pid].name,
					players[from->pid].name, from->text);
				net->SendToOne(from->pid, (byte*)to, len, cfg_msgrel);
			}
			break;
				
		case MSG_INTERARENAPRIV:
			/* FIXME
			 * billcore handles these, but they need to be handled
			 * within server too */
			break;

		case MSG_SYSOPWARNING:
			log->Log(LOG_BADDATA,"Recieved sysop message (%s) (%s)",
					arenas[arena].name, players[pid].name);
			break;

		case MSG_CHAT:
			log->Log(LOG_DEBUG,"Chat message: (%s) %s> %s",
				arenas[arena].name, players[pid].name, from->text);
			/* the billcore module picks these up, so nothing more here */
			break;
	}
}


void SendMessage(int pid, char *str, ...)
{
	int size;
	char _buf[256];
	struct ChatPacket *cp = (struct ChatPacket*)_buf;
	va_list args;

	va_start(args, str);
	size = vsnprintf(cp->text, 250, str, args) + 6;
	va_end(args);

	cp->pktype = S2C_CHAT;
	cp->type = MSG_ARENA;
	cp->sound = 0;
	net->SendToOne(pid, (byte*)cp, size, NET_RELIABLE);
}


