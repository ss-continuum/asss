
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

#define REQUIRE_MOD(m) \
	if (!m) { chat->SendMessage(pid, "Module '" #m "' not loaded"); return; }


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *lm;
local Icmdman *cmd;
local Inet *net;
local Ichatnet *chatnet;
local Iconfig *cfg;
local Icapman *capman;
local Imainloop *ml;
local Iarenaman *aman;
local Igame *game;
local Ilog_file *logfile;
local Iflags *flags;
local Iballs *balls;
local Ifiletrans *filetrans;
local Imodman *mm;

local PlayerData *players;


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

local helptext_t arena_help =
"Targets: none\n"
"Args: [{all}]\n"
"Lists the available arenas. Specifying {all} will also include\n"
"empty arenas that the server knows about.\n";

local void Carena(const char *params, int pid, const Target *target)
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
	seehid = capman && capman->HasCapability(pid, CAP_SEEPRIVARENA);
	aman->LockStatus();
	for (i = 0; (pos-buf) < 480 && i < MAXARENA; i++)
		if (aman->arenas[i].status == ARENA_RUNNING &&
		    ( aman->arenas[i].name[0] != '#' || seehid || i == arena ))
		{
			l = strlen(aman->arenas[i].name) + 1;
			strncpy(pos, aman->arenas[i].name, l);
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


local helptext_t shutdown_help =
"Targets: none\n"
"Args: none\n"
"Immediately shuts down the server.\n";

local void Cshutdown(const char *params, int pid, const Target *target)
{
	byte drop[2] = {0x00, 0x07};
	net->SendToAll(drop, 2, NET_PRI_P5);
	net->SendToAll(drop, 2, NET_PRI_P5);
	ml->Quit();
}


local helptext_t admlogfile_help =
"Targets: none\n"
"Args: {flush} or {reopen}\n"
"Administers the log file that the server keeps. There are two possible\n"
"subcommands: {flush} flushes the log file to disk (in preparation for\n"
"copying it, for example), and {reopen} tells the server to close and\n"
"re-open the log file (to rotate the log while the server is running).\n";

local void Cadmlogfile(const char *params, int pid, const Target *target)
{
	REQUIRE_MOD(logfile)

	if (!strcasecmp(params, "flush"))
		logfile->FlushLog();
	else if (!strcasecmp(params, "reopen"))
		logfile->ReopenLog();
}


local helptext_t flagreset_help =
"Targets: none\n"
"Args: none\n"
"Causes the flag game to immediately reset.\n";

local void Cflagreset(const char *params, int pid, const Target *target)
{
	REQUIRE_MOD(flags)

	flags->FlagVictory(players[pid].arena, -1, 0);
}


local helptext_t ballcount_help =
"Targets: none\n"
"Args: <number of balls to add or remove>\n"
"Increases or decreases the number of balls in the arena. Takes an\n"
"argument that is a positive or negative number, which is the number of\n"
"balls to add (or, if negative, to remove).\n";

local void Cballcount(const char *params, int pid, const Target *target)
{
	int bc, arena = players[pid].arena, add;

	REQUIRE_MOD(balls)

	if (ARENA_OK(arena))
	{
		balls->LockBallStatus(arena);
		bc = balls->balldata[arena].ballcount;
		add = strtol(params, NULL, 0);
		balls->SetBallCount(arena, bc + add);
		balls->UnlockBallStatus(arena);
	}
}


local helptext_t setfreq_help =
"Targets: player, freq, or arena\n"
"Args: <freq number>\n"
"Moves the target player to the specified freq.\n";

local void Csetfreq(const char *params, int pid, const Target *target)
{
	if (!*params)
		return;

	if (target->type == T_PID)
		game->SetFreq(target->u.pid, atoi(params));
	else
	{
		int set[MAXPLAYERS+1], *p, freq = atoi(params);
		pd->TargetToSet(target, set);
		for (p = set; *p != -1; p++)
			game->SetFreq(*p, freq);
	}
}


local helptext_t setship_help =
"Targets: player, freq, or arena\n"
"Args: <ship number>\n"
"Sets the target player to the specified ship. The argument must be a\n"
"number from 1 (Warbird) to 8 (Shark), or 9 (Spec).\n";

local void Csetship(const char *params, int pid, const Target *target)
{
	if (!*params)
		return;

	if (target->type == T_PID)
		game->SetShip(target->u.pid, atoi(params) - 1);
	else
	{
		int set[MAXPLAYERS+1], *p, ship = atoi(params) - 1;
		pd->TargetToSet(target, set);
		for (p = set; *p != -1; p++)
			game->SetShip(*p, ship);
	}
}


local helptext_t version_help =
"Targets: none\n"
"Args: none\n"
"Prints out the version and buildnumber of asss. If the server was built\n"
"with certain options, it might also print out some information about the\n"
"machine that it's running on.\n";

local void Cversion(const char *params, int pid, const Target *target)
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

local helptext_t lsmod_help =
"Targets: none\n"
"Args: none\n"
"Lists all the modules currently loaded into the server.\n";

local void Clsmod(const char *params, int pid, const Target *target)
{
	int p = pid;
	mm->EnumModules(SendModInfo, (void*)&p);
}


local helptext_t insmod_help =
"Targets: none\n"
"Args: <module specifier>\n"
"Immediately loads the specified module into the server.\n";

local void Cinsmod(const char *params, int pid, const Target *target)
{
	int ret;
	ret = mm->LoadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s loaded successfully", params);
	else
		chat->SendMessage(pid, "Loading module %s failed", params);
}


local helptext_t rmmod_help =
"Targets: none\n"
"Args: <module name>\n"
"Attempts to unload the specified module from the server.\n";

local void Crmmod(const char *params, int pid, const Target *target)
{
	int ret;
	ret = mm->UnloadModule(params);
	if (ret == MM_OK)
		chat->SendMessage(pid, "Module %s unloaded successfully", params);
	else
		chat->SendMessage(pid, "Unloading module %s failed", params);
}


local helptext_t getgroup_help =
"Targets: player or none\n"
"Args: none\n"
"Prints out the group of the target player.\n";

local void Cgetgroup(const char *params, int pid, const Target *target)
{
	REQUIRE_MOD(capman)

	if (target->type == T_PID)
		chat->SendMessage(pid, "getgroup: %s is in group %s",
				players[target->u.pid].name,
				capman->GetGroup(target->u.pid));
	else if (target->type == T_ARENA)
		chat->SendMessage(pid, "getgroup: You are in group %s",
				capman->GetGroup(pid));
	else
		chat->SendMessage(pid, "getgroup: Bad target");
}


local helptext_t setgroup_help =
"Targets: player\n"
"Args: [{-a}] [{-t}] <group name>\n"
"Assigns the group given as an argument to the target player. The player\n"
"must be in group {default}, or the server will refuse to change his\n"
"group. Additionally, the player giving the command must have an\n"
"appropriate capability: {setgroup_foo}, where {foo} is the\n"
"group that he's trying to set the target to.\n"
"\n"
"The optional {-t} means to assign the group only for the current\n"
"session. When the target player logs out or changes arenas, the group\n"
"will be lost.\n"
"\n"
"The optional {-a} means to make the assignment local to the current\n"
"arena, rather than being valid in the entire zone.\n";

local void Csetgroup(const char *params, int pid, const Target *target)
{
	int perm = 1, global = 1, t = target->u.pid;
	char cap[MAXGROUPLEN+10];

	REQUIRE_MOD(capman)

	if (!*params) return;
	if (target->type != T_PID) return;

	while (*params && strchr(params, ' '))
	{
		if (!strncmp(params, "temp", 4) || !strncmp(params, "-t", 2))
			perm = 0;
		if (!strncmp(params, "arena", 5) || !strncmp(params, "-a", 2))
			global = 0;
		params = strchr(params, ' ') + 1;
	}
	if (!*params) return;

	/* make sure the setter has permissions to set people to this group */
	snprintf(cap, MAXGROUPLEN+10, "setgroup_%s", params);
	if (!capman->HasCapability(pid, cap))
	{
		lm->Log(L_WARN, "<playercmd> [%s] doesn't have permission to set to group '%s'",
				players[pid].name, params);
		return;
	}

	/* make sure the target isn't in a group already */
	if (strcasecmp(capman->GetGroup(t), "default"))
	{
		lm->Log(L_WARN, "<playercmd> [%s] tried to set the group of [%s],"
				"who is in '%s' already, to '%s'",
				players[pid].name, players[t].name,
				capman->GetGroup(t), params);
		return;
	}

	if (perm)
	{
		time_t tm = time(NULL);
		char info[128];

		snprintf(info, 128, "set by %s on ", players[pid].name);
		ctime_r(&tm, info + strlen(info));
		RemoveCRLF(info);

		capman->SetPermGroup(t, params, global, info);
		chat->SendMessage(pid, "%s is now in group %s",
				players[t].name, params);
		chat->SendMessage(t, "You have been assigned to group %s by %s",
				params, players[pid].name);
	}
	else
	{
		capman->SetTempGroup(t, params);
		chat->SendMessage(pid, "%s is now temporarily in group %s",
				players[t].name, params);
		chat->SendMessage(t, "You have temporarily been assigned to group %s by %s",
				params, players[pid].name);
	}
}


local helptext_t listmods_help =
"Targets: none\n"
"Args: none\n"
"Lists all staff members logged on, which arena they belong to, and\n"
"which group they are in.\n";

local void Clistmods(const char *params, int pid, const Target *target)
{
	int i;
	const char *group;

	if (!capman) return;

	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING &&
		    strcmp(group = capman->GetGroup(i), "default"))
			chat->SendMessage(pid, "listmods: %20s %10s %10s",
					players[i].name,
					aman->arenas[players[i].arena].name,
					group);
}


local helptext_t netstats_help =
"Targets: none\n"
"Args: none\n"
"Prints out some statistics from the network layer, including the number\n"
"of main menu pings the server has received, the total number of packets\n"
"it has sent and received, and the number of buffers currently in use\n"
"versus the number allocated.\n";

local void Cnetstats(const char *params, int pid, const Target *target)
{
	struct net_stats stats;
	net->GetStats(&stats);
	chat->SendMessage(pid, "netstats: pings=%d  pkts sent=%d  pkts recvd=%d",
			stats.pcountpings, stats.pktsent, stats.pktrecvd);
	chat->SendMessage(pid, "netstats: buffers used=%d/%d (%.1f%%)",
			stats.buffersused, stats.buffercount,
			(double)stats.buffersused/(double)stats.buffercount*100.0);
	chat->SendMessage(pid, "netstats: priority counts=%d/%d/%d/%d/%d/%d/%d",
			stats.pri_stats[1], stats.pri_stats[2], stats.pri_stats[3],
			stats.pri_stats[4], stats.pri_stats[5], stats.pri_stats[6],
			stats.pri_stats[7]);
}


local helptext_t info_help =
"Targets: player\n"
"Args: none\n"
"Displays various information on the target player, including which\n"
"client they are using, their resolution, ip address, how long they have\n"
"been connected, and bandwidth usage information.\n";

local void Cinfo(const char *params, int pid, const Target *target)
{
	if (target->type != T_PID)
		chat->SendMessage(pid, "info: must use on a player");
	else
	{
		static const char *type_names[] =
		{
			"unknown", "fake", "vie", "cont", "chat"
		};
		const char *type, *prefix;
		unsigned int tm;
		int t = target->u.pid;
		struct PlayerData *p = players + t;

		type = p->type < (sizeof(type_names)/sizeof(type_names[0])) ?
			type_names[p->type] : "really_unknown";
		prefix = params[0] ? params : "info";
		tm = GTC() - p->connecttime;

		chat->SendMessage(pid,
				"%s: pid=%d  status=%d  name='%s'  squad='%s'",
				prefix, t, p->status, p->name, p->squad);
		chat->SendMessage(pid,
				"%s: arena=%d  type=%s  res=%dx%d  seconds=%d",
				prefix, p->arena, type, p->xres, p->yres, tm / 100);
		if (IS_STANDARD(t))
		{
			struct net_client_stats s;
			net->GetClientStats(t, &s);
			chat->SendMessage(pid,
					"%s: ip=%s  port=%d  encname=%s",
					prefix, s.ipaddr, s.port, s.encname);
			chat->SendMessage(pid,
					"%s: limit=%d  avg bandwidth in/out=%d/%d",
					prefix, s.limit, s.byterecvd*100/tm, s.bytesent*100/tm);
		}
		else if (IS_CHAT(t))
		{
			struct chat_client_stats s;
			chatnet->GetClientStats(t, &s);
			chat->SendMessage(pid,
					"%s: ip=%s  port=%d  encname=%s",
					prefix, s.ipaddr, s.port);
		}
	}
}


local helptext_t setcm_help =
"Targets: player or arena\n"
"Args: see description\n"
"Modifies the chat mask for the target player, or if no target, for the\n"
"current arena. The arguments must all be of the form\n"
"{(-|+)(pub|pubmacro|freq|nmefreq|priv|chat|modchat|all)}. A minus\n"
"sign and then a word disables that type of chat, and a plus sign enables\n"
"it. The special type {all} means to apply the plus or minus to\n"
"all of the above types.\n"
"\n"
"Examples:\n"
" * If someone is spamming public macros: {:player:?setcm -pubmacro}\n"
" * To disable all blue messages for this arena: {?setcm -pub -pubmacro}\n"
" * An equivalent to *shutup: {:player:?setcm -all}\n"
" * To restore chat to normal: {?setcm +all}\n"
"\n"
"Current limitations: You can't currently restrict a particular\n"
"frequency. Leaving and entering an arena will remove a player's chat\n"
"mask.\n";

local void Csetcm(const char *params, int pid, const Target *target)
{
	chat_mask_t mask;
	const char *c = params;

	/* grab the original mask */
	if (target->type == T_ARENA)
		mask = chat->GetArenaChatMask(target->u.arena);
	else if (target->type == T_PID)
		mask = chat->GetPlayerChatMask(target->u.pid);
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
	if (target->type == T_ARENA)
		chat->SetArenaChatMask(target->u.arena, mask);
	else
		chat->SetPlayerChatMask(target->u.pid, mask);
}

local helptext_t getcm_help =
"Targets: player or arena\n"
"Args: none\n"
"Prints out the chat mask for the target player, or if no target, for the\n"
"current arena. The chat mask specifies which types of chat messages are\n"
"allowed.\n";

local void Cgetcm(const char *params, int pid, const Target *target)
{
	chat_mask_t mask;

	if (target->type == T_ARENA)
		mask = chat->GetArenaChatMask(target->u.arena);
	else if (target->type == T_PID)
		mask = chat->GetPlayerChatMask(target->u.pid);
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


local helptext_t a_help =
"Targets: player, freq, or arena\n"
"Args: <text>\n"
"Displays the text as an arena (green) message to the targets.\n";

local void Ca(const char *params, int pid, const Target *target)
{
	int set[MAXPLAYERS+1];
	pd->TargetToSet(target, set);
	chat->SendSetMessage(set, "%s  -%s", params, players[pid].name);
}


local helptext_t warpto_help =
"Targets: player, freq, or arena\n"
"Args: <x coord> <y coord>\n"
"Warps target player to coordinate x,y.\n";

local void Cwarpto(const char *params, int pid, const Target *target)
{
	char *next;
	int x, y;
	
	x = strtol(params, &next, 0);
	if (next == params) return;
	while (*next == ',' || *next == ' ') next++;
	y = strtol(next, NULL, 0);
	if (x == 0 || y == 0) return;
	game->WarpTo(target, x, y);
}


local helptext_t shipreset_help =
"Targets: player, freq, or arena\n"
"Args: none\n"
"Resets the target players' ship(s).\n";

local void Cshipreset(const char *params, int pid, const Target *target)
{
	byte pkt = S2C_SHIPRESET;
	int set[MAXPLAYERS+1];

	pd->TargetToSet(target, set);
	net->SendToSet(set, &pkt, 1, NET_RELIABLE);
}


local helptext_t sheep_help = NULL;

local void Csheep(const char *params, int pid, const Target *target)
{
	int arena = players[pid].arena;
	const char *sheepmsg = NULL;

	if (target->type != T_ARENA)
		return;

	if (ARENA_OK(arena))
		sheepmsg = cfg->GetStr(aman->arenas[arena].cfg, "Misc", "SheepMessage");

	if (sheepmsg)
		chat->SendSoundMessage(pid, 24, sheepmsg);
	else
		chat->SendSoundMessage(pid, 24, "Sheep successfully cloned -- hello Dolly");
}


local helptext_t specall_help =
"Targets: player, freq, or arena\n"
"Args: none\n"
"Sends all of the targets to spectator mode.\n";

local void Cspecall(const char *params, int pid, const Target *target)
{
	int set[MAXPLAYERS+1], *p, arena, freq;

	arena = players[pid].arena;
	if (ARENA_BAD(arena))
		return;

	freq = cfg->GetInt(aman->arenas[arena].cfg, "Team", "SpectatorFrequency", 8025);

	pd->TargetToSet(target, set);
	for (p = set; *p != -1; p++)
		game->SetFreqAndShip(*p, SPEC, freq);
}


local helptext_t getg_help =
"Targets: none\n"
"Args: section:key\n"
"Displays the value of a global setting. Make sure there are no\n"
"spaces around the colon.\n";

local void Cgetg(const char *params, int pid, const Target *target)
{
	const char *res = cfg->GetStr(GLOBAL, params, NULL);
	if (res)
		chat->SendMessage(pid, "%s=%s", params, res);
	else
		chat->SendMessage(pid, "%s not found", params);
}


local helptext_t setg_help =
"Targets: none\n"
"Args: section:key=value\n"
"Sets the value of a global setting. Make sure there are no\n"
"spaces around either the colon or the equals sign.\n";

local void Csetg(const char *params, int pid, const Target *target)
{
	time_t tm = time(NULL);
	char info[128], key[MAXSECTIONLEN+MAXKEYLEN+2], *k = key;
	const char *t = params;

	snprintf(info, 128, "set by %s on ", players[pid].name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	while (*t && *t != '=' && (k-key) < (MAXSECTIONLEN+MAXKEYLEN))
		*k++ = *t++;
	if (*t != '=') return;
	*k = '\0'; /* terminate key */
	t++; /* skip over = */

	cfg->SetStr(GLOBAL, key, NULL, t, info);
}

local helptext_t geta_help =
"Targets: none\n"
"Args: section:key\n"
"Displays the value of an arena setting. Make sure there are no\n"
"spaces around the colon.\n";

local void Cgeta(const char *params, int pid, const Target *target)
{
	int arena = players[pid].arena;
	const char *res;

	if (ARENA_BAD(arena)) return;

	res = cfg->GetStr(aman->arenas[arena].cfg, params, NULL);
	if (res)
		chat->SendMessage(pid, "%s=%s", params, res);
	else
		chat->SendMessage(pid, "%s not found", params);
}

local helptext_t seta_help =
"Targets: none\n"
"Args: section:key=value\n"
"Sets the value of an arena setting. Make sure there are no\n"
"spaces around either the colon or the equals sign.\n";

local void Cseta(const char *params, int pid, const Target *target)
{
	int arena = players[pid].arena;
	time_t tm = time(NULL);
	char info[128], key[MAXSECTIONLEN+MAXKEYLEN+2], *k = key;
	const char *t = params;

	if (ARENA_BAD(arena)) return;

	snprintf(info, 128, "set by %s on ", players[pid].name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	while (*t && *t != '=' && (k-key) < (MAXSECTIONLEN+MAXKEYLEN))
		*k++ = *t++;
	if (*t != '=') return;
	*k = '\0'; /* terminate key */
	t++; /* skip over = */

	cfg->SetStr(aman->arenas[arena].cfg, key, NULL, t, info);
}


local helptext_t prize_help =
"Targets: player, freq, or arena\n"
"Args: see description\n"
"Gives the specified prizes to the target player(s).\n"
"\n"
"Prizes are specified with an optional count, and then a prize name (e.g.\n"
"{3 reps}, {anti}). Negative prizes can be specified with a '-' before\n"
"the prize name or the count (e.g. {-prox}, {-3 bricks}, {5 -guns}). More\n"
"than one prize can be specified in one command. A count without a prize\n"
"name means {random}. For compatability, numerical prize ids with {#} are\n"
"supported.\n";

local void Cprize(const char *params, int pid, const Target *target)
{
#define BAD_TYPE 10000
	const char *tmp = NULL;
	char word[32];
	int i, type, count = 1, t;
	enum { last_none, last_count, last_word } last = last_none;
	struct
	{
		const char *string;
		int type;
	}
	lookup[] =
	{
		{ "random",    0 },
		{ "charge",   13 }, /* must come before "recharge" */
		{ "x",         6 }, /* must come before "prox" */
		{ "recharge",  1 },
		{ "energy",    2 },
		{ "rot",       3 },
		{ "stealth",   4 },
		{ "cloak",     5 },
		{ "warp",      7 },
		{ "gun",       8 },
		{ "bomb",      9 },
		{ "bounce",   10 },
		{ "thrust",   11 },
		{ "speed",    12 },
		{ "shutdown", 14 },
		{ "multi",    15 },
		{ "prox",     16 },
		{ "super",    17 },
		{ "shield",   18 },
		{ "shrap",    19 },
		{ "anti",     20 },
		{ "rep",      21 },
		{ "burst",    22 },
		{ "decoy",    23 },
		{ "thor",     24 },
		{ "mprize",   25 },
		{ "brick",    26 },
		{ "rocket",   27 },
		{ "port",     28 },
	};

	while (strsplit(params, " ,", word, sizeof(word), &tmp))
		if ((t = strtol(word, NULL, 0)) != 0)
		{
			/* this is a count */
			count = t;
			last = last_count;
		}
		else /* try a word */
		{
			/* negative prizes are marked with negative counts, for now */
			if (word[0] == '-')
				count = -count;

			/* now try to find the word */
			type = BAD_TYPE;
			if (word[0] == '#')
				type = strtol(word+1, NULL, 0);
			else
				for (i = 0; i < sizeof(lookup)/sizeof(lookup[0]); i++)
					if (strstr(word, lookup[i].string))
						type = lookup[i].type;

			if (type != BAD_TYPE)
			{
				/* but for actually sending them, they must be marked with
				 * negative types and positive counts. */
				if (count < 0)
				{
					type = -type;
					count = -count;
				}

				game->GivePrize(target, type, count);

				/* reset count to 1 once we hit a successful word */
				count = 1;
			}

			last = last_word;
		}

	if (last == last_count)
		/* if the line ends in a count, do that many of random */
		game->GivePrize(target, 0, count);

#undef BAD_TYPE
}


local helptext_t flaginfo_help =
"Targets: none\n"
"Args: none\n"
"Displays information (status, location, carrier) about all the flags in\n"
"the arena.\n";

local void Cflaginfo(const char *params, int pid, const Target *target)
{
	struct ArenaFlagData *fd;
	int arena, i;

	REQUIRE_MOD(flags)

	if (PID_BAD(pid) || ARENA_BAD(players[pid].arena) || target->type != T_ARENA)
		return;

	arena = players[pid].arena;

	flags->LockFlagStatus(arena);

	fd = flags->flagdata + arena;

	for (i = 0; i < fd->flagcount; i++)
		switch (fd->flags[i].state)
		{
			case FLAG_NONE:
				chat->SendMessage(pid,
						"flaginfo: Flag %d doesn't exist",
						i);
				break;

			case FLAG_ONMAP:
				chat->SendMessage(pid,
						"flaginfo: Flag %d is on the map at (%d,%d), owned by freq %d",
						i, fd->flags[i].x, fd->flags[i].y, fd->flags[i].freq);
				break;

			case FLAG_CARRIED:
				chat->SendMessage(pid,
						"flaginfo: Flag %d is carried by %s",
						i, players[fd->flags[i].carrier].name);
				break;

			case FLAG_NEUTED:
				chat->SendMessage(pid,
						"flaginfo: Flag %d was neuted and has not reappeared yet",
						i);
				break;
		}

	flags->UnlockFlagStatus(arena);
}


local helptext_t neutflag_help =
"Targets: none\n"
"Args: <flag id>\n"
"Neuts the specified flag in the middle of the arena.\n";

local void Cneutflag(const char *params, int pid, const Target *target)
{
	int flagid, arena = players[pid].arena;
	char *next;

	REQUIRE_MOD(flags)

	flags->LockFlagStatus(arena);

	flagid = strtol(params, &next, 0);
	if (next == params || flagid < 0 || flagid >= flags->flagdata[arena].flagcount)
		chat->SendMessage(pid, "neutflag: Bad flag id");
	else
		/* set flag state to none, so that the flag timer will neut it
		 * next time it runs. */
		flags->flagdata[arena].flags[flagid].state = FLAG_NONE;

	flags->UnlockFlagStatus(arena);
}


local helptext_t moveflag_help =
"Targets: none\n"
"Args: <flag id> <owning freq> [<x coord> <y coord>]\n"
"Moves the specified flag. You must always specify the freq that will own\n"
"the flag. The coordinates are optional: if they are specified, the flag\n"
"will be moved there, otherwise it will remain where it is.\n";

local void Cmoveflag(const char *params, int pid, const Target *target)
{
	/* syntax: ?moveflag <flagid> <freq> <x> <y> */
	char *next, *next2;
	int flagid, x, y, freq, arena = players[pid].arena;

	REQUIRE_MOD(flags)

	if (ARENA_BAD(arena))
		return;

	flags->LockFlagStatus(arena);

	flagid = strtol(params, &next, 0);
	if (next == params || flagid < 0 || flagid >= flags->flagdata[arena].flagcount)
	{
		chat->SendMessage(pid, "moveflag: Bad flag id");
		goto mf_unlock;
	}

	freq = strtol(next, &next2, 0);
	if (next == next2)
	{
		/* bad freq */
		chat->SendMessage(pid, "moveflag: Bad freq");
		goto mf_unlock;
	}

	x = strtol(next2, &next, 0);
	while (*next == ',' || *next == ' ') next++;
	y = strtol(next, NULL, 0);
	if (x == 0 || y == 0)
	{
		/* missing coords */
		x = flags->flagdata[arena].flags[flagid].x;
		y = flags->flagdata[arena].flags[flagid].y;
	}

	flags->MoveFlag(arena, flagid, x, y, freq);

mf_unlock:
	flags->UnlockFlagStatus(arena);
}


local helptext_t reloadconf_help =
"Targets: none\n"
"Args: none\n"
"Causes the server to check all config files for modifications since\n"
"they were last loaded, and reload any modified files.\n";

local void Creloadconf(const char *params, int pid, const Target *target)
{
	cfg->CheckModifiedFiles();
	chat->SendMessage(pid, "Reloading all modified config files");
}



local helptext_t getfile_help = NULL;

local void Cgetfile(const char *params, int pid, const Target *target)
{
	const char *basename = strrchr(params, '/');
	basename = basename ? basename + 1 : params;
	filetrans->SendFile(pid, params, basename);
}


local helptext_t putfile_help = NULL;

local void Cputfile(const char *params, int pid, const Target *target)
{
	const char *basename = strrchr(params, '/');
	basename = basename ? basename + 1 : params;
	filetrans->RequestFile(pid, params, basename);
}




/* command group system */

/* declarations */
struct cmd_info
{
	const char *cmdname;
	CommandFunc func;
	helptext_t *phelptext;
};

struct interface_info
{
	void **ptr;
	const char *iid;
};

struct cmd_group
{
	const char *groupname;
	const struct interface_info *ifaces;
	const struct cmd_info *cmds;
	int loaded;
};


/* loading/unloading funcs */

local int load_cmd_group(struct cmd_group *grp)
{
	const struct interface_info *ii;
	const struct cmd_info *ci;

	for (ii = grp->ifaces; ii->iid; ii++)
	{
		*(ii->ptr) = mm->GetInterface(ii->iid, ALLARENAS);
		if (*(ii->ptr) == NULL)
		{
			/* if we can't get one, roll back all the others */
			for (ii--; ii >= grp->ifaces; ii--)
				mm->ReleaseInterface(*(ii->ptr));
			return MM_FAIL;
		}
	}

	for (ci = grp->cmds; ci->cmdname; ci++)
		cmd->AddCommand(ci->cmdname, ci->func, *ci->phelptext);

	grp->loaded = 1;

	return MM_OK;
}

local void unload_cmd_group(struct cmd_group *grp)
{
	const struct interface_info *ii;
	const struct cmd_info *ci;

	if (!grp->loaded)
		return;

	for (ii = grp->ifaces; ii->iid; ii++)
		mm->ReleaseInterface(*(ii->ptr));

	for (ci = grp->cmds; ci->cmdname; ci++)
		cmd->RemoveCommand(ci->cmdname, ci->func);

	grp->loaded = 0;
}

/* loading/unloading commands */

local struct cmd_group *find_group(const char *name);

local helptext_t enablecmdgroup_help =
"Targets: none\n"
"Args: <command group>\n"
"Enables all the commands in the specified command group. This is only\n"
"useful after using ?disablecmdgroup.\n";

local void Cenablecmdgroup(const char *params, int pid, const Target *target)
{
	struct cmd_group *grp = find_group(params);
	if (grp)
	{
		if (grp->loaded)
			chat->SendMessage(pid, "Command group %s already enabled", params);
		else if (load_cmd_group(grp) == MM_OK)
			chat->SendMessage(pid, "Command group %s enabled", params);
		else
			chat->SendMessage(pid, "Error enabling command group %s", params);
	}
	else
		chat->SendMessage(pid, "Command group %s not found");
}

local helptext_t disablecmdgroup_help =
"Targets: none\n"
"Args: <command group>\n"
"Disables all the commands in the specified command group and released the\n"
"modules that they require. This can be used to release interfaces so that\n"
"modules can be unloaded or upgraded without unloading playercmd (which would\n"
"be irreversable).\n";

local void Cdisablecmdgroup(const char *params, int pid, const Target *target)
{
	struct cmd_group *grp = find_group(params);
	if (grp)
	{
		if (grp->loaded)
		{
			unload_cmd_group(grp);
			chat->SendMessage(pid, "Command group %s disabled", params);
		}
		else
			chat->SendMessage(pid, "Command group %s not loaded", params);
	}
	else
		chat->SendMessage(pid, "Command group %s not found", params);
}


/* actual group definitions */

#define CMD(x) {#x, C ## x, & x ## _help},
#define CMD_GROUP(x) {#x, x ## _requires, x ## _commands, 0},
#define REQUIRE(name, iid) {(void**)&name, iid},
#define END() {0}

local const struct interface_info core_requires[] =
{
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(net, I_NET)
	REQUIRE(chatnet, I_CHATNET)
	REQUIRE(ml, I_MAINLOOP)
	END()
};
local const struct cmd_info core_commands[] =
{
	CMD(enablecmdgroup)
	CMD(disablecmdgroup)
	CMD(arena)
	CMD(shutdown)
	CMD(version)
	CMD(lsmod)
	CMD(insmod)
	CMD(rmmod)
	CMD(info)
	CMD(a)
	CMD(netstats)
	END()
};


local const struct interface_info game_requires[] =
{
	REQUIRE(net, I_NET)
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(game, I_GAME)
	REQUIRE(cfg, I_CONFIG)
	END()
};
local const struct cmd_info game_commands[] =
{
	CMD(setfreq)
	CMD(setship)
	CMD(specall)
	CMD(warpto)
	CMD(shipreset)
	CMD(prize)
	END()
};


local const struct interface_info config_requires[] =
{
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(cfg, I_CONFIG)
	END()
};
local const struct cmd_info config_commands[] =
{
	CMD(setg)
	CMD(getg)
	CMD(seta)
	CMD(geta)
	CMD(reloadconf)
	END()
};


local const struct interface_info flag_requires[] =
{
	REQUIRE(flags, I_FLAGS)
	END()
};
local const struct cmd_info flag_commands[] =
{
	CMD(flagreset)
	CMD(flaginfo)
	CMD(neutflag)
	CMD(moveflag)
	END()
};


local const struct interface_info ball_requires[] =
{
	REQUIRE(balls, I_BALLS)
	END()
};
local const struct cmd_info ball_commands[] =
{
	CMD(ballcount)
	END()
};


local const struct interface_info admin_requires[] =
{
	REQUIRE(filetrans, I_FILETRANS)
	END()
};
local const struct cmd_info admin_commands[] =
{
	CMD(getfile)
	CMD(putfile)
	END()
};


local const struct interface_info misc_requires[] =
{
	REQUIRE(capman, I_CAPMAN)
	REQUIRE(aman, I_ARENAMAN)
	REQUIRE(lm, I_LOGMAN)
	REQUIRE(logfile, I_LOG_FILE)
	REQUIRE(cfg, I_CONFIG)
	END()
};
local const struct cmd_info misc_commands[] =
{
	CMD(getgroup)
	CMD(setgroup)
	CMD(listmods)
	CMD(setcm)
	CMD(getcm)
	CMD(sheep)
	CMD(admlogfile)
	END()
};


/* list of groups */
local struct cmd_group all_cmd_groups[] =
{
	CMD_GROUP(core)
	CMD_GROUP(game)
	CMD_GROUP(config)
	CMD_GROUP(flag)
	CMD_GROUP(ball)
	CMD_GROUP(admin)
	CMD_GROUP(misc)
	END()
};

#undef CMD
#undef CMD_GROUP
#undef REQUIRE
#undef END

struct cmd_group *find_group(const char *name)
{
	struct cmd_group *grp;
	for (grp = all_cmd_groups; grp->groupname; grp++)
		if (!strcasecmp(grp->groupname, name))
			return grp;
	return NULL;
}



EXPORT int MM_playercmd(int action, Imodman *_mm, int arena)
{
	struct cmd_group *grp;

	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!pd || !chat || !cmd) return MM_FAIL;

		players = pd->players;

		for (grp = all_cmd_groups; grp->groupname; grp++)
			load_cmd_group(grp);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		for (grp = all_cmd_groups; grp->groupname; grp++)
			unload_cmd_group(grp);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		return MM_OK;
	}
	return MM_FAIL;
}


