
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

#define KEY_CHAT 47


local void SendMessage_(int pid, const char *str, ...);


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
#ifdef CFG_PERSISTENT_CHAT_MASKS
local Ipersist *persist;
#endif

local PlayerData *players;
local ArenaData *arenas;

local chat_mask_t arena_mask[MAXARENA];
local struct player_mask_t
{
	chat_mask_t mask;
	/* this is zero for a session-long mask, otherwise a time() value */
	time_t expires;
	/* a count of messages. this decays exponentially 50% per second */
	int msgs;
	unsigned lastcheck;
} player_mask[MAXPLAYERS];

local int cfg_msgrel, cfg_floodlimit, cfg_floodshutup;


local void expire_mask(int pid)
{
	struct player_mask_t *pm = player_mask + pid;
	int d;

	/* handle expiring masks */
	if (PID_OK(pid) && pm->expires > 0)
		if (time(NULL) > pm->expires)
		{
			pm->mask = 0;
			pm->expires = 0;
		}

	/* handle exponential decay of msg count */
	d = (GTC() - pm->lastcheck) / 100;
	pm->msgs >>= d;
	pm->lastcheck += d * 100;
}

local void check_flood(int pid)
{
	struct player_mask_t *pm = player_mask + pid;

	pm->msgs++;

	if (pm->msgs >= cfg_floodlimit && cfg_floodlimit > 0)
	{
		pm->msgs >>= 1;
		pm->mask |= MSG_PUBMACRO | MSG_PUB | MSG_FREQ | MSG_NMEFREQ | MSG_PRIV | MSG_INTERARENAPRIV | MSG_CHAT | MSG_MODCHAT | MSG_BCOMMAND;
		if (pm->expires)
			/* already has a mask, add time */
			pm->expires += cfg_floodshutup * 60;
		else
			pm->expires = time(NULL) + cfg_floodshutup * 60;
		SendMessage_(pid, "You have been shut up for %d minutes for flooding.", cfg_floodshutup);
		lm->LogP(L_INFO, "chat", pid, "flooded chat, shut up for %d minutes", cfg_floodshutup);
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

local void SendMessage_(int pid, const char *str, ...)
{
	int set[] = { pid, -1 };
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

local void SendSetMessage(int *set, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

local void SendSoundMessage(int pid, char sound, const char *str, ...)
{
	int set[] = { pid, -1 };
	va_list args;
	va_start(args, str);
	v_send_msg(set, MSG_ARENA, sound, str, args);
	va_end(args);
}

local void SendSetSoundMessage(int *set, char sound, const char *str, ...)
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
		if (players[i].status == S_PLAYING &&
		    (arena == ALLARENAS || players[i].arena == arena))
			set[setc++] = i;
	pd->UnlockStatus();
	set[setc] = -1;
}

local void SendArenaMessage(int arena, const char *str, ...)
{
	int set[MAXPLAYERS+1];
	va_list args;

	get_arena_set(set, arena);

	va_start(args, str);
	v_send_msg(set, MSG_ARENA, 0, str, args);
	va_end(args);
}

local void SendArenaSoundMessage(int arena, char sound, const char *str, ...)
{
	int set[MAXPLAYERS+1];
	va_list args;

	get_arena_set(set, arena);

	va_start(args, str);
	v_send_msg(set, MSG_ARENA, sound, str, args);
	va_end(args);
}

local void SendAnyMessage(int *set, char type, char sound, const char *str, ...)
{
	va_list args;
	va_start(args, str);
	v_send_msg(set, type, sound, str, args);
	va_end(args);
}


/* incoming chat handling functions */

#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define CMD_CHAR_3 '!'
#define MOD_CHAT_CHAR '\\'

#define OK(type) IS_ALLOWED(player_mask[pid].mask | arena_mask[arena], type)


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


local void handle_pub(int pid, const char *msg, int ismacro)
{
	int arena = players[pid].arena;
	struct ChatPacket *to = alloca(strlen(msg) + 40);

	if (msg[0] == CMD_CHAR_1 || msg[0] == CMD_CHAR_2 || msg[0] == CMD_CHAR_3)
	{
		if (OK(MSG_COMMAND))
		{
			Target target;
			target.type = T_ARENA;
			target.u.arena = players[pid].arena;
			run_commands(msg, pid, &target);
		}
	}
	else if (OK(ismacro ? MSG_PUBMACRO : MSG_PUB))
	{
		to->pktype = S2C_CHAT;
		to->type = ismacro ? MSG_PUBMACRO : MSG_PUB;
		to->sound = 0;
		to->pid = pid;
		strcpy(to->text, msg);

		lm->LogP(L_DRIVEL, "chat", pid, "Pub msg: %s", msg);

		if (net)
			net->SendToArena(arena, pid, (byte*)to, strlen(msg)+6,
					ismacro ? cfg_msgrel | NET_PRI_N1 : cfg_msgrel);
		if (chatnet)
			chatnet->SendToArena(arena, pid, "MSG:PUB:%s:%s",
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
		if (OK(MSG_COMMAND))
		{
			Target target;
			target.type = T_FREQ;
			target.u.freq.arena = players[pid].arena;
			target.u.freq.freq = freq;
			run_commands(msg, pid, &target);
		}
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

	if (msg[0] == CMD_CHAR_1 || msg[0] == CMD_CHAR_2)
	{
		if (OK(MSG_COMMAND))
		{
			Target target;
			target.type = T_PID;
			target.u.pid = dst;
			run_commands(msg, pid, &target);
		}
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



local void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p;
	int arena = players[pid].arena, freq = players[pid].freq;

	if (ARENA_BAD(arena) || PID_BAD(pid)) return;

	expire_mask(pid);

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

	check_flood(pid);
}


local void MChat(int pid, const char *line)
{
	const char *t;
	char subtype[10], data[24];
	int i;

	if (ARENA_BAD(players[pid].arena)) return;

	expire_mask(pid);

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

	check_flood(pid);
}




/* chat mask stuff */

local chat_mask_t GetArenaChatMask(int arena)
{
	return ARENA_OK(arena) ? arena_mask[arena] : 0;
}

local void SetArenaChatMask(int arena, chat_mask_t mask)
{
	if (ARENA_OK(arena))
		arena_mask[arena] = mask;
}

local chat_mask_t GetPlayerChatMask(int pid)
{
	expire_mask(pid);
	return PID_OK(pid) ? player_mask[pid].mask : 0;
}

local void SetPlayerChatMask(int pid, chat_mask_t mask, int timeout)
{
	if (PID_OK(pid))
	{
		player_mask[pid].mask = mask;
		player_mask[pid].expires = (timeout == 0) ? 0 : time(NULL) + timeout;
	}
}


local void aaction(int arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		ConfigHandle ch = aman->arenas[arena].cfg;
		arena_mask[arena] = cfg->GetInt(ch, "Chat", "RestrictChat", 0);
	}
}


#ifdef CFG_PERSISTENT_CHAT_MASKS

local void clear_data(int pid)
{
	player_mask[pid].mask = 0;
	player_mask[pid].expires = 0;
}

local int get_data(int pid, void *data, int len)
{
	expire_mask(pid);
	if (player_mask[pid].expires)
	{
		memcpy(data, player_mask + pid, sizeof(player_mask[pid]));
		return sizeof(player_mask[pid]);
	}
	else
		return 0;
}

local void set_data(int pid, void *data, int len)
{
	if (len == sizeof(player_mask[pid]))
		memcpy(player_mask + pid, data, sizeof(player_mask[pid]));
}

local PersistentData pdata =
{
	KEY_CHAT, PERSIST_ALLARENAS, INTERVAL_FOREVER,
	get_data, set_data, clear_data
};

#else

local void paction(int pid, int action, int arena)
{
	struct player_mask_t *pm = player_mask + pid;
	pm->mask = pm->expires = pm->msgs = pm->lastcheck = 0;
}

#endif



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

#ifdef CFG_PERSISTENT_CHAT_MASKS
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (!persist) return MM_FAIL;
		persist->RegPlayerPD(&pdata);
#endif

		if (!cfg || !aman || !pd) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

#ifndef CFG_PERSISTENT_CHAT_MASKS
		mm->RegCallback(CB_PLAYERACTION, PAction, ALLARENAS);
#endif
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		cfg_msgrel = cfg->GetInt(GLOBAL, "Chat", "MessageReliable", 1);
		cfg_floodlimit = cfg->GetInt(GLOBAL, "Chat", "FloodLimit", 10);
		cfg_floodshutup = cfg->GetInt(GLOBAL, "Chat", "FloodShutup", 1);

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
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
#ifdef CFG_PERSISTENT_CHAT_MASKS
		persist->UnregPlayerPD(&pdata);
		mm->ReleaseInterface(persist);
#else
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
#endif
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

