
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef WIN32
#define DOUNAME
#define EXTRAARENAS
#endif

#ifdef DOUNAME
#include <sys/utsname.h>
#endif

#ifdef EXTRAARENAS
#include <dirent.h>
#include <unistd.h>
#endif

#include "asss.h"


#define CAP_SEEPRIVARENA "seeprivarena"


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *lm;
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


local void Carena(const char *params, int pid, int target)
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

#ifdef EXTRAARENAS
	/* add in more arenas if requested */
	if (!strcasecmp(params, "all"))
	{
		char aconf[PATH_MAX];
		DIR *dir = opendir("arenas");
		if (dir)
		{
			struct dirent *de;
			while ((de = readdir(dir)))
			{
				/* every arena must have an arena.conf. this filters out
				 * ., .., CVS, etc. */
				snprintf(aconf, PATH_MAX, "arenas/%s/arena.conf", de->d_name);
				if (
						(pos-buf+strlen(de->d_name)) < 480 &&
						access(aconf, R_OK) == 0 &&
						(de->d_name[0] != '#' || seehid)
				   )
				{
					l = strlen(de->d_name) + 1;
					strncpy(pos, de->d_name, l);
					pos += l;
					*pos++ = 0;
					*pos++ = 0;
				}
			}
			closedir(dir);
		}
	}
#endif

	/* send it */
	net->SendToOne(pid, buf, (pos-buf), NET_RELIABLE);
}


local void Cshutdown(const char *params, int pid, int target)
{
	ml->Quit();
}


local void Cadmlogfile(const char *params, int pid, int target)
{
	if (!strcasecmp(params, "flush"))
		logfile->FlushLog();
	else if (!strcasecmp(params, "reopen"))
		logfile->ReopenLog();
}


local void Cflagreset(const char *params, int pid, int target)
{
	flags->FlagVictory(players[pid].arena, -1, 0);
}


local void Cballcount(const char *params, int pid, int target)
{
	int bc, arena = players[pid].arena, add;
	if (ARENA_OK(arena))
	{
		balls->LockBallStatus(arena);
		bc = balls->balldata[arena].ballcount;
		add = atoi(params);
		balls->SetBallCount(arena, bc + add);
		balls->UnlockBallStatus(arena);
	}
}


local void Csetfreq(const char *params, int pid, int target)
{
	if (PID_BAD(target))
		return;

	if (!*params)
		return;

	game->SetFreq(target, atoi(params));
	/* FIXME: allow multiple targets */
}


local void Csetship(const char *params, int pid, int target)
{
	if (PID_BAD(target))
		return;

	if (!*params)
		return;

	game->SetShip(target, atoi(params) - 1);
	/* FIXME: allow multiple targets */
}


local void Cversion(const char *params, int pid, int target)
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

local void Clsmod(const char *params, int pid, int target)
{
	int p = pid;
	mm->EnumModules(SendModInfo, (void*)&p);
}


local void Cinsmod(const char *params, int pid, int target)
{
	int ret;
	ret = mm->LoadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s loaded successfully", params);
	else
		chat->SendMessage(pid, "Loading module %s failed", params);
}


local void Crmmod(const char *params, int pid, int target)
{
	int ret;
	ret = mm->UnloadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s unloaded successfully", params);
	else
		chat->SendMessage(pid, "Unloading module %s failed", params);
}


local void Cgetgroup(const char *params, int pid, int target)
{
	if (PID_OK(pid) && capman)
		chat->SendMessage(pid, "Group: %s", capman->GetGroup(pid));
}


local void Csetgroup(const char *params, int pid, int target)
{
	char cap[MAXGROUPLEN+10];

	if (!*params) return;
	if (PID_BAD(target)) return;
	if (!capman) return;

	/* make sure the setter has permissions to set people to this group */
	snprintf(cap, MAXGROUPLEN+10, "setgroup_%s", params);
	if (!capman->HasCapability(pid, cap))
	{
		lm->Log(L_WARN, "<playercmd> [%s] doesn't have permission to set to group '%s'",
				players[pid].name, params);
		return;
	}

	/* make sure the target isn't in a group already */
	if (strcasecmp(capman->GetGroup(target), "default"))
	{
		lm->Log(L_WARN, "<playercmd> [%s] tried to set the group of [%s],"
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


local void Cnetstats(const char *params, int pid, int target)
{
	struct net_stats stats;
	net->GetStats(&stats);
	chat->SendMessage(pid, "netstats: %d pings, %d packets sent, %d packets recvd",
			stats.pcountpings, stats.pktsent, stats.pktrecvd);
	chat->SendMessage(pid, "netstats: %d/%d (%.1f%%) buffers used",
			stats.buffersused, stats.buffercount,
			(double)stats.buffersused/(double)stats.buffercount*100.0);
}


local void Csetcm(const char *params, int pid, int target)
{
	chat_mask_t mask;
	const char *c = params;

	/* grab the original mask */
	if (target == TARGET_ARENA)
		mask = chat->GetArenaChatMask(players[pid].arena);
	else if (PID_OK(target))
		mask = chat->GetPlayerChatMask(target);
	else
	{
		chat->SendMessage(pid, "setcm: Bad target");
		return;
	}

	/* change it */
	for (;;)
	{
		chat_mask_t newmask = 0;
		int all = 0;

		/* move to next + or - */
		while (*c != '\0' && *c != '-' && *c != '+')
			c++;
		if (*c == '\0')
			break;

		/* figure out which thing to change */
		c++;
		if (!strncasecmp(c, "all", 3))
			all = 1;
		if (all || !strncasecmp(c, "pubmacro", 8))
			newmask |= 1 << MSG_PUBMACRO;
		if (all || !strncasecmp(c, "pub", 3))
			newmask |= 1 << MSG_PUB;
		if (all || !strncasecmp(c, "freq", 4))
			newmask |= 1 << MSG_FREQ;
		if (all || !strncasecmp(c, "nmefreq", 7))
			newmask |= 1 << MSG_NMEFREQ;
		if (all || !strncasecmp(c, "priv", 4))
			newmask |= (1 << MSG_PRIV) | (1 << MSG_INTERARENAPRIV);
		if (all || !strncasecmp(c, "chat", 4))
			newmask |= 1 << MSG_CHAT;
		if (all || !strncasecmp(c, "modchat", 7))
			newmask |= 1 << MSG_MODCHAT;

		/* change it */
		if (c[-1] == '+')
			mask &= ~newmask;
		else
			mask |= newmask;
	}

	/* and install it back where it came from */
	if (target == TARGET_ARENA)
		chat->SetArenaChatMask(players[pid].arena, mask);
	else
		chat->SetPlayerChatMask(pid, mask);
}

local void Cgetcm(const char *params, int pid, int target)
{
	chat_mask_t mask;

	if (target == TARGET_ARENA)
		mask = chat->GetArenaChatMask(players[pid].arena);
	else if (PID_OK(target))
		mask = chat->GetPlayerChatMask(target);
	else
	{
		chat->SendMessage(pid, "getcm: Bad target");
		return;
	}

	chat->SendMessage(pid,
			"getcm: %cpub %cpubmacro %cfreq %cnmefreq %cpriv %cchat %cmodchat",
			IS_RESTRICTED(mask, MSG_PUB) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_PUBMACRO) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_FREQ) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_NMEFREQ) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_PRIV) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_CHAT) ? '-' : '+',
			IS_RESTRICTED(mask, MSG_MODCHAT) ? '-' : '+'
			);
}

local void Ca(const char *params, int pid, int target)
{
	int arena = players[pid].arena;

	if (target == TARGET_ARENA)
	{
		int set[MAXPLAYERS], setc = 0, i;
		pd->LockStatus();
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status == S_PLAYING && players[i].arena == arena)
				set[setc++] = i;
		pd->UnlockStatus();
		set[setc] = -1;
		chat->SendSetMessage(set, "%s -%s", params, players[pid].name);
	}
	else if (PID_OK(target))
		chat->SendMessage(target, "%s - %s", params, players[pid].name);
}



local struct
{
	const char *cmdname;
	CommandFunc func;
}
const all_commands[] =
{
#define CMD(x) {#x, C ## x} /* yay for the preprocessor */
	CMD(arena),
	CMD(shutdown),
	CMD(admlogfile),
	CMD(flagreset),
	CMD(ballcount),
	CMD(setfreq),
	CMD(setship),
	CMD(version),
	CMD(lsmod),
	CMD(insmod),
	CMD(rmmod),
	CMD(getgroup),
	CMD(setgroup),
	CMD(netstats),
	CMD(setcm),
	CMD(getcm),
	CMD(a),
	{ NULL }
#undef CMD
};


EXPORT int MM_playercmd(int action, Imodman *_mm, int arena)
{
	int i;
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface("playerdata", ALLARENAS);
		chat = mm->GetInterface("chat", ALLARENAS);
		lm = mm->GetInterface("logman", ALLARENAS);
		cmd = mm->GetInterface("cmdman", ALLARENAS);
		net = mm->GetInterface("net", ALLARENAS);
		cfg = mm->GetInterface("config", ALLARENAS);
		capman = mm->GetInterface("capman", ALLARENAS);
		aman = mm->GetInterface("arenaman", ALLARENAS);
		game = mm->GetInterface("game", ALLARENAS);
		ml = mm->GetInterface("mainloop", ALLARENAS);
		logfile = mm->GetInterface("log_file", ALLARENAS);
		flags = mm->GetInterface("flags", ALLARENAS);
		balls = mm->GetInterface("balls", ALLARENAS);

		if (!cmd || !net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		for (i = 0; all_commands[i].cmdname; i++)
			cmd->AddCommand(all_commands[i].cmdname, all_commands[i].func);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		for (i = 0; all_commands[i].cmdname; i++)
			cmd->RemoveCommand(all_commands[i].cmdname, all_commands[i].func);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(logfile);
		mm->ReleaseInterface(flags);
		mm->ReleaseInterface(balls);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


