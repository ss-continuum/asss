
#include <string.h>

#include "asss.h"


#define KEY_STATS 1


/* structs */

/* the treap key is the statid */
typedef struct stat_info
{
	TreapHead head;
	int value;
	unsigned int started; /* for timers only */
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
	return si;
}


local void IncrementStat(int pid, int stat, int amount)
{
	stat_info *si;

	if (PID_OK(pid))
	{
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


local inline void update_timer(stat_info *si, unsigned gtc)
{
	if (si->started)
	{
		si->value += (gtc - si->started + 50) / 100;
		si->started = gtc;
		si->dirty = 1;
	}
}

local inline void start_timer(stat_info *si, unsigned gtc)
{
	if (si->started)
		update_timer(si, gtc);
	else
		si->started = gtc;
}

local inline void stop_timer(stat_info *si, unsigned gtc)
{
	update_timer(si, gtc);
	si->started = 0;
}


local void StartTimer(int pid, int stat)
{
	stat_info *si;
	unsigned gtc = GTC();

	if (PID_OK(pid))
	{
		pd->LockPlayer(pid);

#define INC(iv) \
		if ((si = (stat_info*)TrGet((TreapHead*)iv[pid], stat)) == NULL) \
		{ \
			si = new_stat(stat); \
			TrPut((TreapHead**)(iv + pid), (TreapHead*)si); \
		} \
		start_timer(si, gtc);

		INC(forever_stats)
		INC(reset_stats)
		INC(game_stats)
#undef INC

		pd->UnlockPlayer(pid);
	}
}


local void StopTimer(int pid, int stat)
{
	stat_info *si;
	unsigned gtc = GTC();

	if (PID_OK(pid))
	{
		pd->LockPlayer(pid);

#define INC(iv) \
		if ((si = (stat_info*)TrGet((TreapHead*)iv[pid], stat)) == NULL) \
		{ \
			si = new_stat(stat); \
			TrPut((TreapHead**)(iv + pid), (TreapHead*)si); \
		} \
		stop_timer(si, gtc);

		INC(forever_stats)
		INC(reset_stats)
		INC(game_stats)
#undef INC

		pd->UnlockPlayer(pid);
	}
}


/* call with player locked */
local inline void set_stat(int pid, int stat, stat_info **arr, int val)
{
	stat_info *si = (stat_info*)TrGet((TreapHead*)arr[pid], stat);
	if (!si)
	{
		si = new_stat(stat);
		TrPut((TreapHead**)(arr + pid), (TreapHead*)si);
	}
	si->value = val;
	si->started = 0; /* setting a stat stops any timers that were running */
	si->dirty = 1;
}

local void SetStat(int pid, int stat, int interval, int amount)
{
	stat_info **arr = get_array(interval);
	if (PID_OK(pid) && arr)
	{
		pd->LockPlayer(pid);
		set_stat(pid, stat, arr, amount);
		pd->UnlockPlayer(pid);
	}
}


/* call with player locked */
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


/* utility functions for doing stuff to stat treaps */

#ifdef this_wont_be_necessary_until_new_protocol
local void update_timers_work(TreapHead *node, void *clos)
{
	update_timer((stat_info*)node, *(unsigned*)clos);
}

local void update_timers(stat_info *si, unsigned gtc)
{
	TrEnum((TreapHead*)si, (void*)&gtc, update_timers_work);
}
#endif


local void dirty_count_work(TreapHead *node, void *clos)
{
	if (((stat_info*)node)->dirty) (*(int*)clos)++;
}

local int dirty_count(stat_info *si)
{
	int c = 0;
	TrEnum((TreapHead*)si, &c, dirty_count_work);
	return c;
}


/* stuff dealing with stat protocol */

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
			if (dirty_count(reset_stats[pid]))
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


/* stuff dealing with persistant storage */

struct stored_stat
{
	unsigned short stat;
	int value;
};

struct get_stats_clos
{
	struct stored_stat *ss;
	int left;
	unsigned gtc;
};

local void get_stats_enum(TreapHead *node, void *clos_)
{
	struct get_stats_clos *clos = (struct get_stats_clos*)clos_;
	struct stat_info *si = (stat_info*)node;
	if (clos->left > 0)
	{
		update_timer(si, clos->gtc);
		clos->ss->stat = node->key;
		clos->ss->value = si->value;
		clos->ss++;
		clos->left--;
	}
}

local void clear_stats_enum(TreapHead *node, void *clos)
{
	stat_info *si = (stat_info*)node;
	if (si->value)
		si->dirty = 1;
	si->started = 0;
	si->value = 0;
}

#define DO_PERSISTENT_DATA(ival, code)                                         \
                                                                               \
local int get_##ival##_data(int pid, void *data, int len)                      \
{                                                                              \
    struct get_stats_clos clos = { data, len / sizeof(struct stored_stat),     \
        GTC() };                                                               \
    TrEnum((TreapHead*)ival##_stats[pid], &clos, get_stats_enum);              \
    return (byte*)clos.ss - (byte*)data;                                       \
}                                                                              \
                                                                               \
local void set_##ival##_data(int pid, void *data, int len)                     \
{                                                                              \
    struct stored_stat *ss = (struct stored_stat*)data;                        \
    for ( ; len >= sizeof(struct stored_stat);                                 \
            ss++, len -= sizeof(struct stored_stat))                           \
        set_stat(pid, ss->stat, ival##_stats, ss->value);                      \
}                                                                              \
                                                                               \
local void clear_##ival##_data(int pid)                                        \
{                                                                              \
    TrEnum((TreapHead*)ival##_stats[pid], NULL, clear_stats_enum);             \
}                                                                              \
                                                                               \
local PersistentData my_##ival##_data =                                        \
{                                                                              \
    KEY_STATS, ALLARENAS, code,                                                \
    get_##ival##_data, set_##ival##_data, clear_##ival##_data                  \
};

DO_PERSISTENT_DATA(forever, INTERVAL_FOREVER)
DO_PERSISTENT_DATA(reset, INTERVAL_RESET)
DO_PERSISTENT_DATA(game, INTERVAL_GAME)

#undef DO_PERSISTENT_DATA



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
	IncrementStat, StartTimer, StopTimer,
	SetStat, GetStat, SendUpdates
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

		persist->RegPersistentData(&my_forever_data);
		persist->RegPersistentData(&my_reset_data);
		persist->RegPersistentData(&my_game_data);

		mm->RegInterface(&_myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		persist->UnregPersistentData(&my_forever_data);
		persist->UnregPersistentData(&my_reset_data);
		persist->UnregPersistentData(&my_game_data);

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


