
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "asss.h"

#define MAXMSGS 10


typedef struct periodic_msgs
{
	int die, count, arena;
	struct
	{
		const char *msg;
		int interval;
	} msgs[MAXMSGS];
} periodic_msgs;


local Iconfig *cfg;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ichat *chat;
local Imainloop *ml;

local pthread_mutex_t msgmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&msgmtx)
#define UNLOCK() pthread_mutex_unlock(&msgmtx)

local periodic_msgs *msgs[MAXARENA];



local int msg_timer(void *v)
{
	int i;
	periodic_msgs *pm = (periodic_msgs*)v;

	if (!pm->die)
	{
		pm->count++;
		for (i = 0; i < MAXMSGS; i++)
			if (pm->msgs[i].msg && pm->msgs[i].interval > 0)
				if ((pm->count % pm->msgs[i].interval) == 0)
					chat->SendArenaMessage(pm->arena, "%s", pm->msgs[i].msg);
		return TRUE;
	}
	else
	{
		for (i = 0; i < MAXARENA; i++)
			afree(pm->msgs[i].msg);
		afree(pm);
		return FALSE;
	}
}


/* handles only greetmessages */
local void paction(int pid, int action, int arena)
{
	if (action == PA_ENTERARENA)
	{
		int arena = pd->players[pid].arena;
		ConfigHandle ch = ARENA_OK(arena) ? aman->arenas[arena].cfg : NULL;
		const char *msg = ch ? cfg->GetStr(ch, "Misc", "GreetMessage") : NULL;

		if (msg)
			chat->SendMessage(pid, "%s", msg);
	}
}


/* call with lock */
local void remove_msgs(int arena)
{
	periodic_msgs *pm = msgs[arena];

	/* the timer will notice this and free the struct */
	if (pm)
		pm->die = 1;

	msgs[arena] = NULL;
}

/* starts timer to handle periodmessages */
local void aaction(int arena, int action)
{
	LOCK();
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		int i, c = 0;
		periodic_msgs *pm;

		remove_msgs(arena);

		pm = amalloc(sizeof(*pm));
		pm->die = 0;
		pm->count = 0;
		pm->arena = arena;

		for (i = 0; i < MAXMSGS; i++)
		{
			char key[32];
			const char *v;

			pm->msgs[i].msg = NULL;
			pm->msgs[i].interval = 0;

			snprintf(key, sizeof(key), "PeriodicMessage%d", i);

			v = cfg->GetStr(aman->arenas[arena].cfg, "Misc", key);

			if (v)
			{
				char *next;
				int interval = strtol(v, &next, 0);
				if (next)
				{
					/* we don't use this value */
					strtol(next, &next, 0);
					if (next)
					{
						/* skip spaces */
						while (*next && isspace(*next)) next++;
						if (*next)
						{
							pm->msgs[i].interval = interval;
							pm->msgs[i].msg = astrdup(next);
							c++;
						}
					}
				}
			}
		}

		if (c)
		{
			ml->SetTimer(msg_timer, 6000, 6000, pm);
			msgs[arena] = pm;
		}
		else
			afree(pm);
	}
	else if (action == AA_DESTROY)
	{
		remove_msgs(arena);
	}
	UNLOCK();
}



EXPORT int MM_messages(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!cfg || !aman || !pd || !chat || !ml) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);

		ml->ClearTimer(msg_timer);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	return MM_FAIL;
}

