
#include "asss.h"

/* structs */

struct DumbStats /* 80 bytes */
{
	int messages[10];
	int commands, modchat;
	int logins;
	int pad7, pad6, pad5, pad4, pad3, pad2, pad1;
};


/* prototypes */

local void PChat(int, byte *, int);
local void CStats(const char *, int, int);

local void GetDumb(int, void *);
local void SetDumb(int, void *);
local void ClearDumb(int);
local void PAction(int, int);


/* global data */

local Inet *net;
local Imodman *mm;
local Icmdman *cmd;
local Ipersist *persist;
local Ichat *chat;

local const PersistantData dumbdata =
{ 0x15222, sizeof(struct DumbStats), 1, GetDumb, SetDumb, ClearDumb };

local struct DumbStats thedata[MAXPLAYERS];



int MM_dumbstats(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_PERSIST, &persist);
		mm->RegInterest(I_CHAT, &chat);

		if (!net || !cmd || !persist) return MM_FAIL;

		mm->RegCallback(CALLBACK_PLAYERACTION, PAction);
		cmd->AddCommand("stats", CStats, 0);
		persist->RegPersistantData(&dumbdata);
		net->AddPacket(C2S_CHAT, PChat);
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_CHAT, PChat);
		persist->UnregPersistantData(&dumbdata);
		cmd->RemoveCommand("stats", CStats);
		mm->UnregCallback(CALLBACK_PLAYERACTION, PAction);

		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_PERSIST, &persist);
	}
	return MM_OK;
}


#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define MOD_CHAT_CHAR '\\'

void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p;

	if (len <= sizeof(struct ChatPacket)) return;

	if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
		thedata[pid].commands++;
	else if (from->type == MSG_PUB && from->text[0] == MOD_CHAT_CHAR)
		thedata[pid].modchat++;
	else if (from->type >= 0 && from->type < 10)
		thedata[pid].messages[(int)from->type]++;
}

void PAction(int pid, int action)
{
	if (action == PA_CONNECT)
		thedata[pid].logins++;
}

void GetDumb(int pid, void *space)
{
	memcpy(space, thedata + pid, sizeof(struct DumbStats));
}

void SetDumb(int pid, void *space)
{
	memcpy(thedata + pid, space, sizeof(struct DumbStats));
}

void ClearDumb(int pid)
{
	memset(thedata + pid, 0, sizeof(struct DumbStats));
}

void CStats(const char *params, int pid, int target)
{
	struct DumbStats *d;

	if (target == TARGET_ARENA) target = pid;

	d = thedata + target;

	if (target >= 0 && target < MAXPLAYERS && chat)
	{
		chat->SendMessage(pid, "%6d logins", d->logins);
		chat->SendMessage(pid, "%6d public messages", d->messages[MSG_PUB]);
		chat->SendMessage(pid, "%6d private messages", d->messages[MSG_PRIV]);
		chat->SendMessage(pid, "%6d team messages", d->messages[MSG_FREQ]);
		chat->SendMessage(pid, "%6d other-team messages", d->messages[MSG_NMEFREQ]);
		chat->SendMessage(pid, "%6d remote private messages", d->messages[MSG_INTERARENAPRIV]);
		chat->SendMessage(pid, "%6d chat messages", d->messages[MSG_CHAT]);
		chat->SendMessage(pid, "%6d mod chat messages", d->modchat);
		chat->SendMessage(pid, "%6d commands", d->commands);
	}
}

