
#include <string.h>

#include "asss.h"


#define KEY_STATS 1


/* structs */

/* the treap key is the statid */
typedef struct stat_info
{
	TreapHead head;
	int value;
	byte dirty;
} stat_info;


/* global data */

local Imodman *mm;
local Inet *net;
local Iplayerdata *pd;
local Icmdman *cmd;
local Ipersist *persist;
local Ichat *chat;

/* these are protected by the player mutex */
local stat_info *forever_stats[MAXPLAYERS];
local stat_info *reset_stats[MAXPLAYERS];
local stat_info *game_stats[MAXPLAYERS];


/* functions */

local stat_info **get_array(int interval)
{
	switch (interval)
	{
		case INTERVAL_FOREVER: return forever_stats;
		case INTERVAL_RESET: return reset_stats;
		case INTERVAL_GAME: return game_stats;
		default: return NULL;
	}
}

local stat_info *new_stat(int stat)
{
	stat_info *si = amalloc(sizeof(*si));
	si->head.key = stat;
	si->value = 0;
	si->dirty = 0;
	return si;
}


local void IncrementStat(int pid, int stat, int amount)
{
	if (PID_OK(pid))
	{
		stat_info *si;

		pd->LockPlayer(pid);

#define INC(iv) \
		if ((si = (stat_info*)TrGet((TreapHead*)iv[pid], stat)) == NULL) \
		{ \
			si = new_stat(stat); \
			TrPut((TreapHead**)(iv + pid), (TreapHead*)si); \
		} \
		si->value += amount; \
		si->dirty = 1;

		INC(forever_stats)
		INC(reset_stats)
		INC(game_stats)
#undef INC

		pd->UnlockPlayer(pid);
	}
}


local void SetStat(int pid, int stat, int interval, int amount)
{
	stat_info **arr = get_array(interval);
	if (PID_OK(pid) && arr)
	{
		stat_info *si;

		pd->LockPlayer(pid);
		si = (stat_info*)TrGet((TreapHead*)arr[pid], stat);
		if (!si)
		{
			si = new_stat(stat);
			TrPut((TreapHead**)(arr + pid), (TreapHead*)si);
		}
		si->value = amount;
		si->dirty = 1;
		pd->UnlockPlayer(pid);
	}
}

local inline int get_stat(int pid, int stat, stat_info **arr)
{
	stat_info *si = (stat_info*)TrGet((TreapHead*)arr[pid], stat);
	return si ? si->value : 0;
}

local int GetStat(int pid, int stat, int iv)
{
	if (PID_OK(pid))
	{
		int val;
		stat_info **arr = get_array(iv);
		if (!arr)
			return 0;
		pd->LockPlayer(pid);
		val = get_stat(pid, stat, arr);
		pd->UnlockPlayer(pid);
		return val;
	}
	else
		return 0;
}


local void dirty_count_work(TreapHead *node, void *clos)
{
	if (((stat_info*)node)->dirty) (*(int*)clos)++;
}

local int dirty_count(int pid, stat_info **arr)
{
	int c = 0;
	TrEnum((TreapHead*)arr[pid], &c, dirty_count_work);
	return c;
}



#include "packets/scoreupd.h"

void SendUpdates(void)
{
	int pid;
	struct ScorePacket sp = { S2C_SCOREUPDATE };
	struct PlayerData *p;

	/* printf("DEBUG: SendUpdates running...\n"); */

	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (pd->players[pid].status == S_PLAYING)
		{
			pd->LockPlayer(pid);
			if (dirty_count(pid, reset_stats))
			{
				p = pd->players + pid;

				sp.pid = pid;
				sp.killpoints = p->killpoints = get_stat(pid, STAT_KILL_POINTS, reset_stats);
				sp.flagpoints = p->flagpoints = get_stat(pid, STAT_FLAG_POINTS, reset_stats);
				sp.kills = p->wins = get_stat(pid, STAT_KILLS, reset_stats);
				sp.deaths = p->losses = get_stat(pid, STAT_DEATHS, reset_stats);

				pd->UnlockPlayer(pid);

				net->SendToArena(
						p->arena,
						-1,
						(char*)&sp,
						sizeof(sp),
						NET_UNRELIABLE | NET_PRI_N1);
				/* printf("DEBUG: SendUpdates sent scores of %s\n", pd->players[pid].name); */
			}
			else
				pd->UnlockPlayer(pid);
		}
}


struct stored_stat
{
	unsigned short stat;
	int value;
};

struct get_stats_clos
{
	struct stored_stat *ss;
	int left;
};

local void get_stats_enum(TreapHead *node, void *clos_)
{
	struct get_stats_clos *clos = (struct get_stats_clos*)clos_;
	if (clos->left > 0)
	{
		clos->ss->stat = node->key;
		clos->ss->value = ((stat_info*)node)->value;
		clos->ss++;
		clos->left--;
	}
}

#define DO_PERSISTANT_DATA(ival, code)                                         \
                                                                               \
local int get_##ival##_data(int pid, void *data, int len)                      \
{                                                                              \
    struct get_stats_clos clos = { data, len / sizeof(struct stored_stat) };   \
    TrEnum((TreapHead*)ival##_stats[pid], &clos, get_stats_enum);              \
    return (byte*)clos.ss - (byte*)data;                                       \
}                                                                              \
                                                                               \
local void set_##ival##_data(int pid, void *data, int len)                     \
{                                                                              \
    struct stored_stat *ss = (struct stored_stat*)data;                        \
    for ( ; len >= sizeof(struct stored_stat);                                 \
            ss++, len -= sizeof(struct stored_stat))                           \
    {                                                                          \
        struct stat_info *si = new_stat((int)ss->stat);                        \
        si->value = ss->value;                                                 \
        TrPut((TreapHead**)(ival##_stats + pid), (TreapHead*)si);              \
    }                                                                          \
}                                                                              \
                                                                               \
local void clear_##ival##_data(int pid)                                        \
{                                                                              \
    TrEnum((TreapHead*)ival##_stats[pid], NULL, tr_enum_afree);                \
    ival##_stats[pid] = NULL;                                                  \
}                                                                              \
                                                                               \
local PersistantData my_##ival##_data =                                        \
{                                                                              \
    KEY_STATS, ALLARENAS, code,                                                \
    get_##ival##_data, set_##ival##_data, clear_##ival##_data                  \
};

DO_PERSISTANT_DATA(forever, INTERVAL_FOREVER)
DO_PERSISTANT_DATA(reset, INTERVAL_RESET)
DO_PERSISTANT_DATA(game, INTERVAL_GAME)

#undef DO_PERSISTANT_DATA



local helptext_t stats_help =
"Targets: player or none\n"
"Args: none\n"
"Prints out some basic statistics about the target player, or if no\n"
"target, yourself.\n";

struct stat_clos
{
	int pid;
};

local void enum_send_msg(TreapHead *node, void *clos_)
{
	struct stat_clos *clos = (struct stat_clos*)clos_;
	struct stat_info *si = (struct stat_info*)node;
	chat->SendMessage(clos->pid, "  %s: %d", get_stat_name(si->head.key), si->value);
}

local void Cstats(const char *params, int pid, const Target *target)
{
    stat_info **arr;
	struct stat_clos clos = { pid };
	int t = target->type == T_PID ? target->u.pid : pid;

	if (!strcasecmp(params, "forever"))
		arr = forever_stats;
	else if (!strcasecmp(params, "game"))
		arr = game_stats;
	else
		arr = reset_stats;

	if (PID_OK(t) && chat)
	{
		pd->LockPlayer(pid);
		chat->SendMessage(pid,
				"The server is keeping track of the following stats about %s:",
				target->type == T_PID ? pd->players[t].name : "you");
		TrEnum((TreapHead*)arr[pid], &clos, enum_send_msg);
		pd->UnlockPlayer(pid);
	}
}


local Istats _myint =
{
	INTERFACE_HEAD_INIT(I_STATS, "stats")
	IncrementStat, SetStat, GetStat, SendUpdates
};


EXPORT int MM_stats(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!net || !cmd || !persist) return MM_FAIL;

		cmd->AddCommand("stats", Cstats, stats_help);

		persist->RegPersistantData(&my_forever_data);
		persist->RegPersistantData(&my_reset_data);
		persist->RegPersistantData(&my_game_data);

		mm->RegInterface(&_myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		persist->UnregPersistantData(&my_forever_data);
		persist->UnregPersistantData(&my_reset_data);
		persist->UnregPersistantData(&my_game_data);

		cmd->RemoveCommand("stats", Cstats);

		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


