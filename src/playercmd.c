
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock.h>
#endif

#include "asss.h"


#ifdef CFG_EXTRA_VERSION_INFO
#ifndef WIN32
#include <sys/utsname.h>
#endif
#endif

#ifdef CFG_DO_EXTRAARENAS
#ifndef WIN32
#include <dirent.h>
#include <unistd.h>
#else
#include <io.h>
#endif
#endif


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


/* returns 0 if found, 1 if not */
local int check_arena(char *pkt, int len, char *check)
{
	char *pos = pkt + 1;
	while (pos-pkt < len)
	{
		if (strcasecmp(pos, check) == 0)
			return 0;
		/* skip over string, null terminator, and two bytes of count */
		pos += strlen(pos) + 3;
	}
	return 1;
}

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
			*pos++ = (pcount[i] >> 0) & 0xFF;
			*pos++ = (pcount[i] >> 8) & 0xFF;
		}
	aman->UnlockStatus();

#ifdef CFG_DO_EXTRAARENAS
	/* add in more arenas if requested */
	if (!strcasecmp(params, "all"))
	{
#ifndef WIN32
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
						(de->d_name[0] != '#' || seehid) &&
						check_arena(buf, pos-buf, de->d_name)
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
#else
		char aconf[PATH_MAX];
		struct _finddata_t fi;
		long FH = _findfirst("arenas/*", &fi);
		if (FH != -1)
		{
			do
			{
				if ((fi.attrib & _A_SUBDIR) &&
				    strcmp(fi.name, "..") &&
				    strcmp(fi.name, "."))
				{
					/* every arena must have an arena.conf */
					snprintf(aconf, PATH_MAX, "arenas/%s/arena.conf", fi.name);
					if (
							(pos-buf+strlen(fi.name)) < 480 &&
							access(aconf, R_OK) == 0 &&
							(fi.name[0] != '#' || seehid) &&
							check_arena(buf, pos-buf, fi.name)
					   )
					{
						l = strlen(fi.name) + 1;
						strncpy(pos, fi.name, l);
						pos += l;
						*pos++ = 0;
						*pos++ = 0;
					}
				}
			} while (_findnext(FH,&fi) != -1);
			_findclose(FH);
		}
#endif
	}
#endif

	/* send it */
	net->SendToOne(pid, buf, (pos-buf), NET_RELIABLE);
}


local void Cshutdown(const char *params, int pid, int target)
{
	byte drop[2] = {0x00, 0x07};
	net->SendToAll(drop, 2, NET_PRI_P5);
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
	if (!*params)
		return;

	if (PID_OK(target))
		game->SetFreq(target, atoi(params));
	else if (target == TARGET_ARENA)
	{
		int i, arena = pd->players[pid].arena;
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status == S_PLAYING &&
			    players[i].arena == arena)
				game->SetFreq(i, atoi(params));
	}
}


local void Csetship(const char *params, int pid, int target)
{
	if (!*params)
		return;

	if (PID_OK(target))
		game->SetShip(target, atoi(params) - 1);
	else if (target == TARGET_ARENA)
	{
		int i, arena = pd->players[pid].arena;
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status == S_PLAYING &&
			    players[i].arena == arena)
				game->SetFreq(i, atoi(params) - 1);
	}
}


local void Cversion(const char *params, int pid, int target)
{
	chat->SendMessage(pid, "asss %s built at %s", ASSSVERSION, BUILDDATE);
#ifdef CFG_EXTRA_VERSION_INFO
#ifndef WIN32
	{
		struct utsname un;
		uname(&un);
		chat->SendMessage(pid, "running on %s %s, host: %s, machine: %s",
				un.sysname, un.release, un.nodename, un.machine);
	}
#else
	{
		OSVERSIONINFO vi;
		DWORD len;
		char name[MAX_COMPUTERNAME_LENGTH + 1];

		vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&vi);

		len = MAX_COMPUTERNAME_LENGTH + 1;
		GetComputerName(name, &len);

		chat->SendMessage(pid, "running on %s %s (version %d.%d.%d), host: %s",
			vi.dwPlatformId == VER_PLATFORM_WIN32s ? "Windows 3.11" : 
				vi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ? 
					(vi.dwMinorVersion == 0 ? "Windows 95" : "Windows 98") :
				vi.dwPlatformId == VER_PLATFORM_WIN32_NT ? "Windows NT" : "Unknown",
			vi.szCSDVersion,
			vi.dwMajorVersion, vi.dwMinorVersion,
			vi.dwBuildNumber,
			name);
	}
#endif
#endif

#ifdef CFG_LOG_PRIVATE
	chat->SendMessage(pid, "This server IS logging private and chat messages.");
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
	if (PID_OK(target) && capman)
		chat->SendMessage(pid, "getgroup: %s is in group %s",
				players[target].name,
				capman->GetGroup(pid));
	else if (target == TARGET_ARENA && capman)
		chat->SendMessage(pid, "getgroup: You are in group %s",
				capman->GetGroup(pid));
	else
		chat->SendMessage(pid, "getgroup: Bad target");
}


local void Clistmods(const char *params, int pid, int target)
{
	int i;
	const char *group;

	if (!capman) return;

	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING &&
		    strcmp(group = capman->GetGroup(i), "default"))
			chat->SendMessage(pid, "listmods: %20s %10s %10s",
					players[i].name,
					arenas[players[i].arena].name,
					group);
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
	chat->SendMessage(pid, "netstats: pings=%d pkts sent=%d pkts recvd=%d",
			stats.pcountpings, stats.pktsent, stats.pktrecvd);
	chat->SendMessage(pid, "netstats: buffers used=%d/%d (%.1f%%)",
			stats.buffersused, stats.buffercount,
			(double)stats.buffersused/(double)stats.buffercount*100.0);
	chat->SendMessage(pid, "netstats: priority counts=%d/%d/%d/%d/%d/%d/%d",
			stats.pri_stats[1], stats.pri_stats[2], stats.pri_stats[3],
			stats.pri_stats[4], stats.pri_stats[5], stats.pri_stats[6],
			stats.pri_stats[7]);
}


local void Cinfo(const char *params, int pid, int target)
{
	if (PID_BAD(target))
		chat->SendMessage(pid, "info: must use on a player");
	else
	{
		static const char *type_names[4] =
		{
			"unknown", "vie", "cont", "fake"
		};
		struct client_stats s;
		struct PlayerData *p = players + target;
		const char *type, *prefix;
		unsigned int tm;

		net->GetClientStats(target, &s);
		type = p->type < 4 ? type_names[p->type] : "really_unknown";
		prefix = params[0] ? params : "info";
		tm = GTC() - s.connecttime;

		chat->SendMessage(pid,
				"%s: pid=%d  status=%d  name='%s'  squad='%s'",
				prefix, target, p->status, p->name, p->squad);
		chat->SendMessage(pid,
				"%s: arena=%d  type=%s  res=%dx%d",
				prefix, p->arena, type, p->xres, p->yres);
		chat->SendMessage(pid,
				"%s: ip=%s  port=%d  encname=%s",
				prefix, s.ipaddr, s.port, s.encname);
		chat->SendMessage(pid,
				"%s: seconds=%d  limit=%d  avg bandwidth in/out=%d/%d",
				prefix, tm / 100, s.limit,
				s.byterecvd*100/tm, s.bytesent*100/tm);
	}
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
		chat->SendArenaMessage(arena,"%s  -%s",params,players[pid].name);
	else if (PID_OK(target))
		chat->SendMessage(target, "%s  -%s", params, players[pid].name);
}

local void Cwarpto(const char *params, int pid, int target)
{
	int x,y;

	if (sscanf(params,"%d,%d",&x,&y) == 2 &&
		PID_OK(target) && players[target].shiptype != SPEC)
			game->WarpTo(target,x,y);
	else if (sscanf(params,"%d %d",&x,&y) == 2 &&
		PID_OK(target) && players[target].shiptype != SPEC)
			game->WarpTo(target,x,y);
	else
		chat->SendMessage(pid,"warpto format is either: \"*warpto x,y\" or \"*warpto x y\"");
}

local void Cshipreset(const char *params, int pid, int target)
{
	int arena = players[pid].arena;
	byte pkt = S2C_SHIPRESET;

	if (target == TARGET_ARENA)
		net->SendToArena(arena, -1, &pkt, 1, NET_RELIABLE);
	else if (PID_OK(target))
		net->SendToOne(pid, &pkt, 1, NET_RELIABLE);
}

local void Csheep(const char *params, int pid, int target)
{
	int arena = players[pid].arena;
	const char *sheepmsg = NULL;

	if (target != TARGET_ARENA)
		return;

	if (ARENA_OK(arena))
		sheepmsg = cfg->GetStr(arenas[arena].cfg, "Misc", "SheepMessage");

	if (sheepmsg)
		chat->SendSoundMessage(pid, 24, sheepmsg);
	else
		chat->SendSoundMessage(pid, 24, "Sheep successfully cloned -- hello Dolly");
}

local void Cspecall(const char *params, int pid, int target)
{
	int arena = players[pid].arena, i;
	int freq = cfg->GetInt(arenas[arena].cfg, "Team", "SpectatorFrequency", 8025);

	if (target == TARGET_ARENA)
	{
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (players[pid].arena == arena && players[pid].status == S_PLAYING &&
					players[pid].shiptype != SPEC)
					game->SetFreqAndShip(i, SPEC, freq);
		}
	}
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
	CMD(listmods),
	CMD(setgroup),
	CMD(netstats),
	CMD(info),
	CMD(setcm),
	CMD(getcm),
	CMD(a),
	CMD(warpto),
	CMD(shipreset),
	CMD(sheep),
	CMD(specall),
	{ NULL }
#undef CMD
};


EXPORT int MM_playercmd(int action, Imodman *_mm, int arena)
{
	int i;
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		logfile = mm->GetInterface(I_LOG_FILE, ALLARENAS);
		flags = mm->GetInterface(I_FLAGS, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);

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
	return MM_FAIL;
}


