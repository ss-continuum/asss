
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef WIN32
#include <malloc.h>
#endif

#include "asss.h"


#define CAP_MODCHAT "seemodchat"
#define CAP_SENDMODCHAT "sendmodchat"


/* prototypes */

local void SendMessage_(int, const char *, ...);
local void SendSetMessage(int *, const char *, ...);
local void SendSoundMessage(int, char, const char *, ...);
local void SendSetSoundMessage(int *, char, const char *, ...);
local void SendAnyMessage(int *set, char type, char sound, const char *format, ...);
local void SendArenaMessage(int arena, const char *, ...);
local void SendArenaSoundMessage(int, char, const char *, ...);

local chat_mask_t GetArenaChatMask(int arena);
local void SetArenaChatMask(int arena, chat_mask_t mask);
local chat_mask_t GetPlayerChatMask(int pid);
local void SetPlayerChatMask(int pid, chat_mask_t mask);

local void PChat(int, byte *, int);
local void PAction(int pid, int action, int arena);
local void AAction(int arena, int action);


/* global data */

local Iplayerdata *pd;
local Inet *net;
local Iconfig *cfg;
local Ilogman *lm;
local Icmdman *cmd;
local Imodman *mm;
local Iarenaman *aman;
local Icapman *capman;

local PlayerData *players;
local ArenaData *arenas;

local chat_mask_t arena_mask[MAXARENA];
local chat_mask_t player_mask[MAXPLAYERS];

local int cfg_msgrel;


local Ichat _int =
{
	INTERFACE_HEAD_INIT(I_CHAT, "chat")
	SendMessage_, SendSetMessage,
	SendSoundMessage, SendSetSoundMessage,
	SendAnyMessage, SendArenaMessage,
	SendArenaSoundMessage,
	GetArenaChatMask, SetArenaChatMask,
	GetPlayerChatMask, SetPlayerChatMask
};


EXPORT int MM_chat(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		if (!net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		mm->RegCallback(CB_PLAYERACTION, PAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, AAction, ALLARENAS);

		cfg_msgrel = cfg->GetInt(GLOBAL, "Chat", "MessageReliable", 1);
		net->AddPacket(C2S_CHAT, PChat);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		net->RemovePacket(C2S_CHAT, PChat);
		mm->UnregCallback(CB_PLAYERACTION, PAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, AAction, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}


local void run_commands(const char *text, int pid, Target *target)
{
	char buf[512], *b;

	if (!cmd) return;

	while (*text)
	{
		/* skip over *, ?, and | */
		while (*text == '?' || *text == '*' || *text == '|' || *text == '!')
			text++;
		if (*text)
		{
			b = buf;
			while (*text && *text != '|' && (b-buf) < sizeof(buf))
				*b++ = *text++;
			*b = '\0';
			cmd->Command(buf, pid, target);
		}
	}
}


#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define CMD_CHAR_3 '!'
#define MOD_CHAT_CHAR '\\'

void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p, *to;
	int setc = 0, i, arena, freq;
	int set[MAXPLAYERS+1];
	Target target;

#define OK(type) IS_ALLOWED(player_mask[pid] | arena_mask[arena], type)

	freq = players[pid].freq;
	arena = players[pid].arena;
	if (ARENA_BAD(arena) || PID_BAD(pid)) return;

	if (len < 6 || from->text[len - 6] != '\0')
	{
		lm->Log(L_MALICIOUS, "<chat> {%s} [%s] Non-null terminated chat message",
			arenas[arena].name, players[pid].name);
		return;
	}

	to = alloca(len + 40);
	to->pktype = S2C_CHAT;
	to->type = from->type;
	to->sound = 0;
	to->pid = pid;
	strcpy(to->text, from->text);

	switch (from->type)
	{
		case MSG_ARENA:
			lm->Log(L_MALICIOUS, "<chat> {%s} [%s] Recieved arena message",
				arenas[arena].name, players[pid].name);
			break;

		case MSG_PUBMACRO:
			/* fall through */

		case MSG_PUB:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2 ||
					from->text[0] == CMD_CHAR_3)
			{
				target.type = T_ARENA;
				target.u.arena = players[pid].arena;
				run_commands(from->text, pid, &target);
			}
			else if (from->text[0] == MOD_CHAT_CHAR)
			{
				if (capman)
				{
					if (capman->HasCapability(pid, CAP_SENDMODCHAT) &&
							OK(MSG_MODCHAT))
					{
						to->type = MSG_SYSOPWARNING;
						sprintf(to->text, "%s> %s", players[pid].name, from->text+1);
						pd->LockStatus();
						for (i = 0; i < MAXPLAYERS; i++)
							if (players[i].status == S_PLAYING &&
									capman->HasCapability(i, CAP_MODCHAT) &&
									i != pid)
								set[setc++] = i;
						pd->UnlockStatus();
						set[setc] = -1;
						net->SendToSet(set, (byte*)to, strlen(to->text)+6, NET_RELIABLE);
						lm->Log(L_DRIVEL, "<chat> {%s} [%s] Mod chat: %s",
								arenas[arena].name, players[pid].name, from->text+1);
					}
					else
					{
						lm->Log(L_DRIVEL, "<chat> {%s} [%s] Attempted mod chat "
								"(missing cap or shutup): %s",
								arenas[arena].name, players[pid].name, from->text+1);
					}
				}
				else
					SendMessage_(pid, "Mod chat is currently disabled");
			}
			else if (OK(from->type)) /* normal pub message */
			{
				
				lm->Log(L_DRIVEL,"<chat> {%s} [%s] Pub msg: %s",
					arenas[arena].name, players[pid].name, from->text);
				net->SendToArena(arena, pid, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_NMEFREQ:
			freq = players[from->pid].freq;
			/* fall through */

		case MSG_FREQ:
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2 ||
					from->text[0] == CMD_CHAR_3)
			{
				target.type = T_FREQ;
				target.u.freq.arena = players[pid].arena;
				target.u.freq.freq = freq;
				run_commands(from->text, pid, &target);
			}
			else if (OK(from->type))
			{
				lm->Log(L_DRIVEL,"<chat> {%s} [%s] (freq=%i) Freq msg: %s",
					arenas[arena].name, players[pid].name, freq, from->text);
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
			if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2 ||
					from->text[0] == CMD_CHAR_3)
			{
				target.type = T_PID;
				target.u.pid = from->pid;
				run_commands(from->text, pid, &target);
			}
			else if (PID_OK(from->pid) && OK(MSG_PRIV))
			{
#ifdef CFG_LOG_PRIVATE
				lm->Log(L_DRIVEL,"<chat> {%s} [%s] to [%s] Priv msg: %s",
					arenas[arena].name, players[pid].name,
					players[from->pid].name, from->text);
#endif
				net->SendToOne(from->pid, (byte*)to, len, cfg_msgrel);
			}
			break;

		case MSG_INTERARENAPRIV:
			/* FIXME
			 * billcore handles these, but they need to be handled
			 * within server too */
			break;

		case MSG_SYSOPWARNING:
			lm->Log(L_MALICIOUS,"<chat> {%s} [%s] Recieved sysop message",
					arenas[arena].name, players[pid].name);
			break;

		case MSG_CHAT:
#ifdef CFG_LOG_PRIVATE
			lm->Log(L_DRIVEL,"<chat> {%s} [%s] Chat msg: %s",
				arenas[arena].name, players[pid].name, from->text);
#endif
			/* the billcore module picks these up, so nothing more here */
			break;
	}
#undef OK
}


void PAction(int pid, int action, int arena)
{
	player_mask[pid] = 0;
}

void AAction(int arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		ConfigHandle ch = aman->arenas[arena].cfg;
		arena_mask[arena] = cfg->GetInt(ch, "Chat", "RestrictChat", 0);
	}
}


/* message sending funcs */

local void v_send_msg(int *set, char type, char sound, const char *str, va_list ap)
{
	int size;
	char _buf[256];
	struct ChatPacket *cp = (struct ChatPacket*)_buf;

	size = vsnprintf(cp->text, 250, str, ap) + 6;

	cp->pktype = S2C_CHAT;
	cp->type = type;
	cp->sound = sound;
	net->SendToSet(set, (byte*)cp, size, NET_RELIABLE);
}

void SendMessage_(int pid, const char *str, ...)
{
	int set[] = { pid, -1 };
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

void SendSetMessage(int *set, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

void SendSoundMessage(int pid, char sound, const char *str, ...)
{
	int set[] = { pid, -1 };
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, sound, str, args);
	va_end(args);
}

void SendSetSoundMessage(int *set, char sound, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, sound, str, args);
	va_end(args);
}

local void get_arena_set(int *set, int arena)
{
	int setc = 0, i;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING && players[i].arena == arena)
			set[setc++] = i;
	pd->UnlockStatus();
	set[setc] = -1;
}

void SendArenaMessage(int arena, const char *str, ...)
{
	int set[MAXPLAYERS+1];
	va_list args;

	get_arena_set(set, arena);

	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

void SendArenaSoundMessage(int arena, char sound, const char *str, ...)
{
	int set[MAXPLAYERS+1];
	va_list args;

	get_arena_set(set, arena);

	va_start(args, str);
	v_send_msg(set, MSG_ARENA, sound, str, args);
	va_end(args);
}

void SendAnyMessage(int *set, char type, char sound, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	v_send_msg(set, type, sound, str, args);
	va_end(args);
}


/* chat mask stuff */

chat_mask_t GetArenaChatMask(int arena)
{
	return ARENA_OK(arena) ? arena_mask[arena] : 0;
}

void SetArenaChatMask(int arena, chat_mask_t mask)
{
	if (ARENA_OK(arena))
		arena_mask[arena] = mask;
}

chat_mask_t GetPlayerChatMask(int pid)
{
	return PID_OK(pid) ? player_mask[pid] : 0;
}

void SetPlayerChatMask(int pid, chat_mask_t mask)
{
	if (PID_OK(pid))
		player_mask[pid] = mask;
}


