
/* dist: public */

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
local Ilogman *lm;


EXPORT int MM_arenaperm(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

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
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


local int HasPermission(int pid, int arena)
{
	if (ARENA_OK(arena) && aman->arenas[arena].status == ARENA_RUNNING)
	{
		ConfigHandle c = aman->arenas[arena].cfg;
		/* cfghelp: General:NeedCap, arena, string, mod: arenaperm
		 * If this setting is present for an arena, any player entering
		 * the arena must have the capability specified this setting.
		 * This can be used to restrict arenas to certain groups of
		 * players. */
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
				lm->Log(L_WARN, "<arenaperm> [%s] Can't find any unrestricted arena!",
						pd->players[pid].name);
			else
			{
				pd->players[pid].arena = i; /* redirect him to new arena! */
				chat->SendMessage(pid, "You don't have permission to enter arena %s!",
						aman->arenas[arena].name);
				lm->Log(L_INFO, "<arenaperm> [%s] Redirected from arena {%s} to {%s}",
						aman->arenas[arena].name, aman->arenas[i].name);
			}
		}
	}
}

