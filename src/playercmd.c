
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"



/* prototypes */

local void Carena(const char *, int, int);
local void Cshutdown(const char *, int, int);
local void Cflagreset(const char *, int, int);
local void Clogfile(const char *, int, int);
local void Cballcount(const char *, int, int);


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *log;
local Icmdman *cmd;
local Inet *net;
local Iconfig *cfg;
local Imainloop *ml;
local Iarenaman *aman;
local Ilog_file *logfile;
local Iflags *flags;
local Iballs *balls;

local PlayerData *players;
local ArenaData *arenas;

local ConfigHandle configops;



int MM_playercmd(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CHAT, &chat);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_LOG_FILE, &logfile);
		mm->RegInterest(I_FLAGS, &flags);
		mm->RegInterest(I_BALLS, &balls);

		if (!cmd || !net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		cmd->AddCommand("arena", Carena);
		cmd->AddCommand("shutdown", Cshutdown);
		cmd->AddCommand("logfile", Clogfile);
		cmd->AddCommand("flagreset", Cflagreset);
		cmd->AddCommand("ballcount", Cballcount);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("arena", Carena);
		cmd->RemoveCommand("shutdown", Cshutdown);
		cmd->RemoveCommand("logfile", Clogfile);
		cmd->RemoveCommand("flagreset", Cflagreset);
		cmd->RemoveCommand("ballcount", Cballcount);

		cfg->CloseConfigFile(configops);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_MAINLOOP, &ml);
		mm->UnregInterest(I_LOG_FILE, &logfile);
		mm->UnregInterest(I_FLAGS, &flags);
		mm->UnregInterest(I_BALLS, &balls);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}



void Carena(const char *params, int pid, int target)
{
	byte buf[MAXPACKET];
	byte *pos = buf;
	int i = 0, l, pcount[MAXARENA];

	*pos++ = S2C_ARENA;

	memset(pcount, 0, MAXARENA * sizeof(int));

	pd->LockStatus();

	/* count up players */
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING &&
		    players[i].arena >= 0)
			pcount[players[i].arena]++;

	/* signify current arena */
	if (players[pid].arena >= 0)
		pcount[players[pid].arena] *= -1;

	pd->UnlockStatus();

	/* build arena info packet */
	aman->LockStatus();
	for (i = 0; (pos-buf) < 480 && i < MAXARENA; i++)
		if (arenas[i].status == ARENA_RUNNING)
		{
			l = strlen(arenas[i].name) + 1;
			strncpy(pos, arenas[i].name, l);
			pos += l;
			*(short*)pos = pcount[i];
			pos += 2;
		}
	aman->UnlockStatus();

	/* send it */
	net->SendToOne(pid, buf, (pos-buf), NET_RELIABLE);
}


void Cshutdown(const char *params, int pid, int target)
{
	ml->Quit();
}


void Clogfile(const char *params, int pid, int target)
{
	if (!strcasecmp(params, "flush"))
		logfile->FlushLog();
	else if (!strcasecmp(params, "reopen"))
		logfile->ReopenLog();
}


void Cflagreset(const char *params, int pid, int target)
{
	flags->FlagVictory(players[pid].arena, -1, 0);
}


void Cballcount(const char *params, int pid, int target)
{
	int bc, arena = players[pid].arena, add;
	if (arena >= 0 && arena < MAXARENA)
	{
		balls->LockBallStatus(arena);
		bc = balls->balldata[arena].ballcount;
		add = atoi(params);
		if (add == 0)
			add = 1;
		balls->SetBallCount(arena, bc + add);
		balls->UnlockBallStatus(arena);
	}
}


