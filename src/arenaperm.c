
#include "asss.h"


/* callbacks */
local void MyPA(int pid, int action, int arena);

/* local data */
local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icapman *capman;
local Ilogman *log;


EXPORT int MM_arenaperm(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface("playerdata", ALLARENAS);
		aman = mm->GetInterface("arenaman", ALLARENAS);
		cfg = mm->GetInterface("config", ALLARENAS);
		chat = mm->GetInterface("chat", ALLARENAS);
		capman = mm->GetInterface("capman", ALLARENAS);
		log = mm->GetInterface("logman", ALLARENAS);

		mm->RegCallback(CB_PLAYERACTION, MyPA, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(log);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


static int HasPermission(int pid, int arena)
{
	if (ARENA_OK(arena) && aman->arenas[arena].status == ARENA_RUNNING)
	{
		ConfigHandle c = aman->arenas[arena].cfg;
		const char *capname = cfg->GetStr(c, "General", "NeedCap");
		return capname ? capman->HasCapability(pid, capname) : 1;
	}
	else
		return 0;
}


void MyPA(int pid, int action, int arena)
{
	if (action == PA_PREENTERARENA)
	{
		if (! HasPermission(pid, arena))
		{
			/* try to find a place for him */
			int i = 0;
			while (i < MAXARENA && ! HasPermission(pid, i))
				i++;
			if (i == MAXARENA)
				log->Log(L_WARN, "<arenaperm> [%s] Can't find any unrestricted arena!",
						pd->players[pid].name);
			else
			{
				pd->players[pid].arena = i; /* redirect him to new arena! */
				chat->SendMessage(pid, "You don't have permission to enter arena %s!",
						aman->arenas[arena].name);
				log->Log(L_INFO, "<arenaperm> [%s] Redirected from arena {%s} to {%s}",
						aman->arenas[arena].name, aman->arenas[i].name);
			}
		}
	}
}

