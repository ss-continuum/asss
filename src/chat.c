
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
local void MChat(int, const char *);
local void PAction(int pid, int action, int arena);
local void AAction(int arena, int action);


/* global data */

local Iplayerdata *pd;
local Inet *net;
local Ichatnet *chatnet;
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
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		if (!cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		mm->RegCallback(CB_PLAYERACTION, PAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, AAction, ALLARENAS);

		cfg_msgrel = cfg->GetInt(GLOBAL, "Chat", "MessageReliable", 1);

		if (net)
			net->AddPacket(C2S_CHAT, PChat);

		if (chatnet)
			chatnet->AddHandler("SEND", MChat);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		if (net)
			net->RemovePacket(C2S_CHAT, PChat);
		if (chatnet)
			chatnet->RemoveHandler("SEND", MChat);
		mm->UnregCallback(CB_PLAYERACTION, PAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, AAction, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
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

#define OK(type) IS_ALLOWED(player_mask[pid] | arena_mask[arena], type)


local void handle_pub(int pid, const char *msg, int ismacro)
{
	int arena = players[pid].arena;
	struct ChatPacket *to = alloca(strlen(msg) + 40);
	
	if (msg[0] == CMD_CHAR_1 || msg[0] == CMD_CHAR_2 || msg[0] == CMD_CHAR_3)
	{
		Target target;
		target.type = T_ARENA;
		target.u.arena = players[pid].arena;
		run_commands(msg, pid, &target);
	}
	else if (OK(ismacro ? MSG_PUBMACRO : MSG_PUB))
	{
		to->pktype = S2C_CHAT;
		to->type = ismacro ? MSG_PUBMACRO : MSG_PUB;
		to->sound = 0;
		to->pid = pid;
		strcpy(to->text, msg);

		lm->LogP(L_DRIVEL, "chat", pid, "Pub msg: %s", msg);

		if (net) net->SendToArena(arena, pid, (byte*)to, strlen(msg)+6, cfg_msgrel);
		if (chatnet) chatnet->SendToArena(arena, pid, "MSG:PUB:%s:%s",
				players[pid].name,
				msg);
	}
}


local void handle_modchat(int pid, const char *msg)
{
	int arena = players[pid].arena, i;
	int set[MAXPLAYERS+1], setc = 0;
	struct ChatPacket *to = alloca(strlen(msg) + 40);

	to->pktype = S2C_CHAT;
	to->type = MSG_SYSOPWARNING;
	to->sound = 0;
	to->pid = pid;
	sprintf(to->text, "%s> %s", players[pid].name, msg);

	if (capman)
	{
		if (capman->HasCapability(pid, CAP_SENDMODCHAT) && OK(MSG_MODCHAT))
		{
			pd->LockStatus();
			for (i = 0; i < MAXPLAYERS; i++)
				if (players[i].status == S_PLAYING &&
				    capman->HasCapability(i, CAP_MODCHAT) &&
				    i != pid)
					set[setc++] = i;
			pd->UnlockStatus();
			set[setc] = -1;
			if (net) net->SendToSet(set, (byte*)to, strlen(to->text)+6, NET_RELIABLE);
			if (chatnet) chatnet->SendToSet(set, "MSG:MOD:%s:%s",
					players[pid].name, msg);
			lm->LogP(L_DRIVEL, "chat", pid, "Mod chat: %s", msg);
		}
		else
		{
			lm->LogP(L_DRIVEL, "chat", pid, "Attempted mod chat "
					"(missing cap or shutup): %s", msg);
		}
	}
	else
		SendMessage_(pid, "Mod chat is currently disabled");
}


local void handle_freq(int pid, int freq, const char *msg)
{
	int arena = players[pid].arena, i;
	int set[MAXPLAYERS+1], setc = 0;
	struct ChatPacket *to = alloca(strlen(msg) + 40);

	if (msg[0] == CMD_CHAR_1 || msg[0] == CMD_CHAR_2 || msg[0] == CMD_CHAR_3)
	{
		Target target;
		target.type = T_FREQ;
		target.u.freq.arena = players[pid].arena;
		target.u.freq.freq = freq;
		run_commands(msg, pid, &target);
	}
	else if (OK(players[pid].freq == freq ? MSG_FREQ : MSG_NMEFREQ))
	{
		to->pktype = S2C_CHAT;
		to->type = players[pid].freq == freq ? MSG_FREQ : MSG_NMEFREQ;
		to->sound = 0;
		to->pid = pid;
		strcpy(to->text, msg);

		pd->LockStatus();
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].freq == freq &&
				players[i].arena == arena &&
				i != pid)
				set[setc++] = i;
		pd->UnlockStatus();
		set[setc] = -1;

		if (net) net->SendToSet(set, (byte*)to, strlen(to->text)+6, cfg_msgrel);
		if (chatnet) chatnet->SendToSet(set, "MSG:FREQ:%s:%s",
				players[pid].name,
				msg);

		lm->LogP(L_DRIVEL, "chat", pid, "Freq msg (%d): %s", freq, msg);
	}
}


local void handle_priv(int pid, int dst, const char *msg)
{
	int arena = players[pid].arena;
	struct ChatPacket *to = alloca(strlen(msg) + 40);

	if (msg[0] == CMD_CHAR_1 || msg[0] == CMD_CHAR_2 || msg[0] == CMD_CHAR_3)
	{
		Target target;
		target.type = T_PID;
		target.u.pid = dst;
		run_commands(msg, pid, &target);
	}
	else if (OK(MSG_PRIV))
	{
		to->pktype = S2C_CHAT;
		to->type = MSG_PRIV;
		to->sound = 0;
		to->pid = pid;
		strcpy(to->text, msg);
#ifdef CFG_LOG_PRIVATE
		lm->LogP(L_DRIVEL, "chat", pid, "to [%s] Priv msg: %s",
				players[dst].name, msg);
#endif
		if (IS_STANDARD(dst))
			net->SendToOne(dst, (byte*)to, strlen(to->text)+6, NET_RELIABLE);
		else if (IS_CHAT(dst))
			chatnet->SendToOne(dst, "MSG:PRIV:%s:%s",
					players[pid].name, msg);
	}
}


local void handle_chat(int pid, const char *msg)
{
#ifdef CFG_LOG_PRIVATE
	lm->LogP(L_DRIVEL, "chat", pid, "Chat msg: %s", msg);
#endif
}




void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p;
	int arena = players[pid].arena, freq = players[pid].freq;

	if (ARENA_BAD(arena) || PID_BAD(pid)) return;

	if (len < 6 || from->text[len - 6] != '\0')
	{
		lm->LogP(L_MALICIOUS, "chat", pid, "Non-null terminated chat message");
		return;
	}

	switch (from->type)
	{
		case MSG_ARENA:
			lm->LogP(L_MALICIOUS, "chat", pid, "Recieved arena message");
			break;

		case MSG_PUBMACRO:
			/* fall through */
		case MSG_PUB:
			if (from->text[0] == MOD_CHAT_CHAR)
				handle_modchat(pid, from->text + 1);
			else
				handle_pub(pid, from->text, from->type == MSG_PUBMACRO);
			break;

		case MSG_NMEFREQ:
			freq = players[from->pid].freq;
			/* fall through */
		case MSG_FREQ:
			handle_freq(pid, freq, from->text);
			break;

		case MSG_PRIV:
			if (PID_OK(from->pid))
				handle_priv(pid, from->pid, from->text);
			break;

		case MSG_INTERARENAPRIV:
			/* FIXME
			 * billcore handles these, but they need to be handled
			 * within server too */
			break;

		case MSG_SYSOPWARNING:
			lm->LogP(L_MALICIOUS, "chat", pid, "Recieved sysop message");
			break;

		case MSG_CHAT:
			handle_chat(pid, from->text);
			break;
	}
}


void MChat(int pid, const char *line)
{
	const char *t;
	char subtype[10], data[24];
	int i;

	if (ARENA_BAD(players[pid].arena)) return;

	t = delimcpy(subtype, line, sizeof(subtype), ':');
	if (!t) return;

	if (!strcasecmp(subtype, "PUB") || !strcasecmp(subtype, "CMD"))
		handle_pub(pid, t, 0);
	else if (!strcasecmp(subtype, "PRIV") || !strcasecmp(subtype, "PRIVCMD"))
	{
		t = delimcpy(data, t, sizeof(data), ':');
		if (!t) return;
		i = pd->FindPlayer(data);
		if (PID_OK(i))
			handle_priv(pid, i, t);
	}
	else if (!strcasecmp(subtype, "FREQ"))
	{
		t = delimcpy(data, t, sizeof(data), ':');
		if (!t) return;
		i = atoi(data);
		handle_freq(pid, i, t);
	}
	else if (!strcasecmp(subtype, "CHAT"))
		handle_chat(pid, t);
	else if (!strcasecmp(subtype, "MOD"))
		handle_modchat(pid, t);
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

local const char *get_chat_type(int type)
{
	switch (type)
	{
		case MSG_ARENA: return "ARENA";
		case MSG_SYSOPWARNING: return "SYSOP";
		case MSG_INTERARENAPRIV: return "REMOTEPRIV";
		case MSG_CHAT: return "CHAT";
		default: return NULL;
	}
}

local void v_send_msg(int *set, char type, char sound, const char *str, va_list ap)
{
	int size;
	char _buf[256];
	const char *ctype = get_chat_type(type);
	struct ChatPacket *cp = (struct ChatPacket*)_buf;

	size = vsnprintf(cp->text, 250, str, ap) + 6;

	cp->pktype = S2C_CHAT;
	cp->type = type;
	cp->sound = sound;
	cp->pid = -1;
	if (net) net->SendToSet(set, (byte*)cp, size, NET_RELIABLE);
	if (chatnet && ctype)
		chatnet->SendToSet(set, "MSG:%s:%s", ctype, cp->text);
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


