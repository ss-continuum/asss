
#include "asss.h"

/* structs */

struct GlobalStats /* 80 bytes */
{
	int messages[10];
	int commands, modchat;
	int logins;
	int pad7, pad6, pad5, pad4, pad3, pad2, pad1;
};

struct ArenaStats /* 64 bytes */
{
	int stats[STAT_MAX+1];
};


/* prototypes */

local void IncrementStat(int, Stat, int);

local void GetA(int, void *);
local void SetA(int, void *);
local void ClearA(int);

local void GetG(int, void *);
local void SetG(int, void *);
local void ClearG(int);

local void PChat(int, byte *, int);
local void CStats(const char *, int, int);
local void CScores(const char *, int, int);
local void PAFunc(int, int);


/* global data */

local Inet *net;
local Imodman *mm;
local Icmdman *cmd;
local Ipersist *persist;
local Ichat *chat;

local const PersistantData gdatadesc =
{ 0x15222, sizeof(struct GlobalStats), 1, GetG, SetG, ClearG };

local const PersistantData adatadesc =
{ 0x7EE41, sizeof(struct ArenaStats), 0, GetA, SetA, ClearA };

/* the big arrays for stats */
local struct GlobalStats gdata[MAXPLAYERS];
local struct ArenaStats adata[MAXPLAYERS];

local Istats _myint = { IncrementStat };


int MM_stats(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_PERSIST, &persist);
		mm->RegInterest(I_CHAT, &chat);

		if (!net || !cmd || !persist) return MM_FAIL;

		mm->RegCallback(CALLBACK_PLAYERACTION, PAFunc);
		cmd->AddCommand("stats", CStats, 0);
		cmd->AddCommand("scores", CScores, 0);
		persist->RegPersistantData(&gdatadesc);
		persist->RegPersistantData(&adatadesc);
		net->AddPacket(C2S_CHAT, PChat);
		mm->RegInterface(I_STATS, &_myint);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_STATS, &_myint);
		net->RemovePacket(C2S_CHAT, PChat);
		persist->UnregPersistantData(&gdatadesc);
		persist->UnregPersistantData(&adatadesc);
		cmd->RemoveCommand("stats", CStats);
		cmd->RemoveCommand("scores", CScores);
		mm->UnregCallback(CALLBACK_PLAYERACTION, PAFunc);

		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_PERSIST, &persist);
	}
	return MM_OK;
}


void IncrementStat(int pid, Stat stat, int amount)
{
	struct ArenaStats *d = adata + pid;

	if (pid >= 0 && pid < MAXPLAYERS)
		d->stats[stat] += amount;
}


#define CMD_CHAR_1 '?'
#define CMD_CHAR_2 '*'
#define MOD_CHAT_CHAR '\\'

void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p;

	if (len <= sizeof(struct ChatPacket)) return;

	if (from->text[0] == CMD_CHAR_1 || from->text[0] == CMD_CHAR_2)
		gdata[pid].commands++;
	else if (from->type == MSG_PUB && from->text[0] == MOD_CHAT_CHAR)
		gdata[pid].modchat++;
	else if (from->type >= 0 && from->type < 10)
		gdata[pid].messages[(int)from->type]++;
}

void PAFunc(int pid, int action)
{
	if (action == PA_CONNECT)
		gdata[pid].logins++;
}


void GetG(int pid, void *space)
{
	memcpy(space, gdata + pid, sizeof(struct GlobalStats));
}

void SetG(int pid, void *space)
{
	memcpy(gdata + pid, space, sizeof(struct GlobalStats));
}

void ClearG(int pid)
{
	memset(gdata + pid, 0, sizeof(struct GlobalStats));
}

void GetA(int pid, void *space)
{
	memcpy(space, adata + pid, sizeof(struct ArenaStats));
}

void SetA(int pid, void *space)
{
	memcpy(adata + pid, space, sizeof(struct ArenaStats));
}

void ClearA(int pid)
{
	memset(adata + pid, 0, sizeof(struct ArenaStats));
}


void CStats(const char *params, int pid, int target)
{
	struct GlobalStats *d;

	if (target == TARGET_ARENA) target = pid;

	d = gdata + target;

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

void CScores(const char *params, int pid, int target)
{
	struct ArenaStats *d;

	if (target == TARGET_ARENA) target = pid;

	d = adata + target;

	if (target >= 0 && target < MAXPLAYERS && chat)
	{
		chat->SendMessage(pid, "%d kill points, %d flag points",
				d->stats[STAT_KPOINTS], d->stats[STAT_FPOINTS]);
		chat->SendMessage(pid, "%d kills, %d deaths",
				d->stats[STAT_KILLS], d->stats[STAT_DEATHS]);
	}
}

