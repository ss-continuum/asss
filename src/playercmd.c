
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"



/* prototypes */

local void Carena(const char *, int, int);
local void Clogin(const char *, int, int);
local void Csetop(const char *, int, int);
local void Cshutdown(const char *, int, int);


/* global data */

local Ichat *chat;
local Icmdman *cmd;
local Icore *core;
local Inet *net;
local Iconfig *cfg;
local Imainloop *ml;
local PlayerData *players;

local ConfigHandle *configops;



int MM_playercmd(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		chat = mm->GetInterface(I_CHAT);
		cmd = mm->GetInterface(I_CMDMAN);
		core = mm->GetInterface(I_CORE);
		net = mm->GetInterface(I_NET);
		cfg = mm->GetInterface(I_CONFIG);
		ml = mm->GetInterface(I_MAINLOOP);
		if (!chat || !cmd || !core || !net || !cfg || !ml) return MM_FAIL;
		players = mm->players;

		configops = cfg->OpenConfigFile("oplevels");

		cmd->AddCommand("arena", Carena, 0);
		cmd->AddCommand("login", Clogin, 0);
		cmd->AddCommand("setop", Csetop, 0);
		cmd->AddCommand("shutdown", Cshutdown, 200);
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("arena", Carena);
		cmd->RemoveCommand("login", Clogin);
		cmd->RemoveCommand("setop", Csetop);
		cmd->RemoveCommand("shutdown", Cshutdown);

		cfg->CloseConfigFile(configops);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "playercmd - handles standard player commands (?)";
	}
	return MM_OK;
}



void Carena(const char *params, int pid, int target)
{
	static byte buf[MAXPACKET];
	byte *pos = buf;
	int i = 0, l, pcount[MAXARENA];

	*pos++ = S2C_ARENA;

	memset(pcount, 0, MAXARENA * sizeof(int));

	/* count up players */
	for (i = 0; i < MAXPLAYERS; i++)
		if (	net->GetStatus(i) == S_CONNECTED &&
				players[i].arena >= 0)
			pcount[players[i].arena]++;

	/* signify current arena */
	if (players[pid].arena >= 0)
		pcount[players[pid].arena] *= -1;
		
	/* build arena info packet */
	for (i = 0; (pos-buf) < 480 && i < MAXARENA; i++)
		if (core->arenas[i])
		{
			l = strlen(core->arenas[i]->name) + 1;
			strncpy(pos, core->arenas[i]->name, l);
			pos += l;
			*(short*)pos = pcount[i];
			pos += 2;
		}

	/* send it */
	net->SendToOne(pid, buf, (pos-buf), NET_RELIABLE);
}


void Clogin(const char *params, int pid, int target)
{
	int arena = players[pid].arena, op2;

	if (strlen(params))
	{
		players[pid].oplevel = cfg->GetInt(
				configops, "Passwords", params, 0);
	}
	else
	{
		players[pid].oplevel = cfg->GetInt(
				configops, "Staff", players[pid].name, 0);
		if (arena >= 0)
		{
			op2 = cfg->GetInt(core->arenas[arena]->config,
					"Staff", players[pid].name, 0);
			if (op2 > players[pid].oplevel)
				players[pid].oplevel = op2;
		}
	}
	chat->SendMessage(pid, "Your current oplevel is %i", players[pid].oplevel);
}


void Csetop(const char *params, int pid, int target)
{
	int op = atoi(params);

	if (target < 0 || op < 0 || op > 255) return;
	if (op > players[pid].oplevel) return;
	players[target].oplevel = op;
	chat->SendMessage(pid, "You have assigned oplevel %i to %s", op, players[target].name);
	chat->SendMessage(target, "Your current oplevel is %i", op);
}


void Cshutdown(const char *params, int pid, int target)
{
	ml->Quit();
}


