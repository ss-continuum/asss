
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef WIN32
#define DOUNAME
#endif

#ifdef DOUNAME
#include <sys/utsname.h>
#endif

#include "asss.h"


#define CAP_SEEPRIVARENA "seeprivarena"

/* prototypes */

local void Carena(const char *, int, int);
local void Cshutdown(const char *, int, int);
local void Cflagreset(const char *, int, int);
local void Cadmlogfile(const char *, int, int);
local void Cballcount(const char *, int, int);

local void Csetfreq(const char *, int, int);
local void Csetship(const char *, int, int);

local void Cversion(const char *, int, int);
local void Clsmod(const char *, int, int);
local void Cinsmod(const char *, int, int);
local void Crmmod(const char *, int, int);

local void Cgetgroup(const char *, int, int);
local void Csetgroup(const char *, int, int);

/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *log;
local Icmdman *cmd;
local Inet *net;
local Iconfig *cfg;
local Icapman *capman;
local Imainloop *ml;
local Iarenaman *aman;
local Igame *game;
local Ilog_file *logfile;
local Iflags *flags;
local Iballs *balls;
local Imodman *mm;

local PlayerData *players;
local ArenaData *arenas;



EXPORT int MM_playercmd(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CHAT, &chat);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_CAPMAN, &capman);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_GAME, &game);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_LOG_FILE, &logfile);
		mm->RegInterest(I_FLAGS, &flags);
		mm->RegInterest(I_BALLS, &balls);

		if (!cmd || !net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		cmd->AddCommand("arena", Carena);
		cmd->AddCommand("shutdown", Cshutdown);
		cmd->AddCommand("admlogfile", Cadmlogfile);
		cmd->AddCommand("flagreset", Cflagreset);
		cmd->AddCommand("ballcount", Cballcount);
		cmd->AddCommand("setfreq", Csetfreq);
		cmd->AddCommand("setship", Csetship);
		cmd->AddCommand("version", Cversion);
		cmd->AddCommand("lsmod", Clsmod);
		cmd->AddCommand("insmod", Cinsmod);
		cmd->AddCommand("rmmod", Crmmod);
		cmd->AddCommand("getgroup", Cgetgroup);
		cmd->AddCommand("setgroup", Csetgroup);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("arena", Carena);
		cmd->RemoveCommand("shutdown", Cshutdown);
		cmd->RemoveCommand("admlogfile", Cadmlogfile);
		cmd->RemoveCommand("flagreset", Cflagreset);
		cmd->RemoveCommand("ballcount", Cballcount);
		cmd->RemoveCommand("setfreq", Csetfreq);
		cmd->RemoveCommand("setship", Csetship);
		cmd->RemoveCommand("version", Cversion);
		cmd->RemoveCommand("lsmod", Clsmod);
		cmd->RemoveCommand("insmod", Cinsmod);
		cmd->RemoveCommand("rmmod", Crmmod);
		cmd->RemoveCommand("getgroup", Cgetgroup);
		cmd->RemoveCommand("setgroup", Csetgroup);

		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_CAPMAN, &capman);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_GAME, &game);
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
	int i = 0, arena, l, pcount[MAXARENA], seehid;

	*pos++ = S2C_ARENA;
	memset(pcount, 0, MAXARENA * sizeof(int));

	pd->LockStatus();

	arena = players[pid].arena;

	/* count up players */
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING &&
		    players[i].arena >= 0)
			pcount[players[i].arena]++;

	/* signify current arena */
	if (ARENA_OK(players[pid].arena))
		pcount[arena] *= -1;

	pd->UnlockStatus();

	/* build arena info packet */
	seehid = capman->HasCapability(pid, CAP_SEEPRIVARENA);
	aman->LockStatus();
	for (i = 0; (pos-buf) < 480 && i < MAXARENA; i++)
		if (arenas[i].status == ARENA_RUNNING &&
		    ( arenas[i].name[0] != '#' || seehid || i == arena ))
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


void Cadmlogfile(const char *params, int pid, int target)
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
	if (ARENA_OK(arena))
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


void Csetfreq(const char *params, int pid, int target)
{
	if (PID_BAD(target))
		return;

	if (!*params)
		return;

	game->SetFreq(target, atoi(params));
}


void Csetship(const char *params, int pid, int target)
{
	if (PID_BAD(target))
		return;

	if (!*params)
		return;

	game->SetShip(target, atoi(params) - 1);
}


void Cversion(const char *params, int pid, int target)
{
	chat->SendMessage(pid, "asss %s (buildnumber %d)", ASSSVERSION, BUILDNUMBER);
#ifdef DOUNAME
	{
		struct utsname un;
		uname(&un);
		chat->SendMessage(pid, "running on %s %s, host: %s, machine: %s",
				un.sysname, un.release, un.nodename, un.machine);
	}
#endif
}


local void SendModInfo(const char *name, const char *info, void *p)
{
	int pid = *(int*)p;
	if (info)
		chat->SendMessage(pid, "  %s (%s)", name, info);
	else
		chat->SendMessage(pid, "  %s", name, info);
}

void Clsmod(const char *params, int pid, int target)
{
	int p = pid;
	mm->EnumModules(SendModInfo, (void*)&p);
}


void Cinsmod(const char *params, int pid, int target)
{
	int ret;
	ret = mm->LoadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s loaded successfully", params);
	else
		chat->SendMessage(pid, "Loading module %s failed", params);
}


void Crmmod(const char *params, int pid, int target)
{
	int ret;
	ret = mm->UnloadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s unloaded successfully", params);
	else
		chat->SendMessage(pid, "Unloading module %s failed", params);
}


void Cgetgroup(const char *params, int pid, int target)
{
	if (PID_OK(pid) && capman)
		chat->SendMessage(pid, "Group: %s", capman->GetGroup(pid));
}


void Csetgroup(const char *params, int pid, int target)
{
	char cap[MAXGROUPLEN+10];

	if (!*params) return;
	if (PID_BAD(target)) return;
	if (!capman) return;

	/* make sure the setter has permissions to set people to this group */
	snprintf(cap, MAXGROUPLEN+10, "setgroup_%s", params);
	if (!capman->HasCapability(pid, cap))
	{
		log->Log(L_WARN, "<playercmd> [%s] doesn't have permission to set to group '%s'",
				players[pid].name, params);
		return;
	}

	/* make sure the target isn't in a group already */
	if (!strcasecmp(capman->GetGroup(target), "default"))
	{
		log->Log(L_WARN, "<playercmd> [%s] tried to set the group of [%s],"
				"who is in '%s' already, to '%s'",
				players[pid].name, players[target].name,
				capman->GetGroup(target), params);
		return;
	}

	capman->SetGroup(target, params);
	chat->SendMessage(pid, "%s is now in group %s",
			players[target].name, params);
	chat->SendMessage(target, "You have been assigned to group %s by %s",
			params, players[pid].name);
}


