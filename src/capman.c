
#include <string.h>

#include "asss.h"


#define MAXGROUPLEN 32



/* interface funcs */
local int HasCapability(int pid, const char *cap);

/* callbacks */
local void ArenaAction(int arena, int action);
local void PlayerAction(int pid, int action, int arena);

/* data */
local ConfigHandle groupdef, gstaff, astaff[MAXARENA];
local char groups[MAXPLAYERS][MAXGROUPLEN];

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ilogman *log;
local Iconfig *cfg;

local Icapman _myint = { HasCapability };


int MM_capman(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);

		if (!cfg) return MM_FAIL;

		mm->RegCallback(CALLBACK_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CALLBACK_ARENAACTION, ArenaAction, ALLARENAS);

		groupdef = cfg->OpenConfigFile(NULL, "groupdef.conf");
		gstaff = cfg->OpenConfigFile(NULL, "staff.conf");

		mm->RegInterface(I_CAPMAN, &_myint);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_CAPMAN, &_myint);
		cfg->CloseConfigFile(groupdef);
		cfg->CloseConfigFile(gstaff);
		mm->UnregCallback(CALLBACK_ARENAACTION, ArenaAction, ALLARENAS);
		mm->UnregCallback(CALLBACK_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}



void ArenaAction(int arena, int action)
{
	if (action == AA_CREATE || action == AA_DESTROY)
	{
		cfg->CloseConfigFile(astaff[arena]);
	}
	if  (action == AA_CREATE)
	{
		astaff[arena] = cfg->OpenConfigFile(aman->arenas[arena].name, "staff.conf");
	}
}


void PlayerAction(int pid, int action, int arena)
{
	if (action == PA_ENTERARENA)
	{
		char *gg = cfg->GetStr(gstaff, "Staff", pd->players[pid].name);
		char *ag = cfg->GetStr(astaff[arena], "Staff", pd->players[pid].name);
		char *aname = aman->arenas[arena].name;

		if (gg)
		{
			char *t;
			/* check if this is an 'arena:group' thing */
			t = strstr(gg, aname);
			if (t)
			{
				t = strchr(t, ':');
				if (t)
				{
					int pos = 0;
					t++; /* skip ':' */
					while (pos < MAXGROUPLEN && *t && *t != ' ')
						groups[pid][pos] = *t++;
				}
				else
				{
					/* it must not be 'arena:group', so use the whole
					 * thing */
					astrncpy(groups[pid], gg, MAXGROUPLEN);
				}
			}
			else
			{
				/* this must be a group valid everywhere, so it takes
				 * precedence */
				astrncpy(groups[pid], gg, MAXGROUPLEN);
			}
		}
		else if (ag)
		{
			/* use arena-assigned group */
			astrncpy(groups[pid], ag, MAXGROUPLEN);
		}
		else /* just give him the default */
			astrncpy(groups[pid], "default", MAXGROUPLEN);
		log->Log(L_DRIVEL, "<capman> {%s} [%s] Player assigned to group '%s'",
				aname,
				pd->players[pid].name,
				groups[pid]);
	}
	else /* on LEAVEARENA, CONNECT, DISCONNECT, reset group */
	{
		astrncpy(groups[pid], "default", MAXGROUPLEN);
	}
}


int HasCapability(int pid, const char *cap)
{
	if (cfg->GetStr(groupdef, groups[pid], cap))
		return 1;
	else
		return 0;
}


