
#include <string.h>

#include "asss.h"



/* interface funcs */
local int HasCapability(int pid, const char *cap);
local char *GetGroup(int pid);
local void SetGroup(int pid, const char *group);

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

local Icapman _myint = { HasCapability, GetGroup, SetGroup };


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

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

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
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
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
		astaff[arena] = NULL;
	}
	if  (action == AA_CREATE)
	{
		astaff[arena] = cfg->OpenConfigFile(aman->arenas[arena].name, "staff.conf");
	}
}


local void UpdateGroup(int pid, int arena)
{
#define LOGIT(from) \
	log->Log(L_DRIVEL, "<capman> {%s} [%s] Player assigned to group '%s' from %s", \
			aname, \
			pd->players[pid].name, \
			groups[pid], \
			from)

	if (ARENA_BAD(arena))
	{
		/* only global groups available for now */
		char *gg = cfg->GetStr(gstaff, "Staff", pd->players[pid].name);
		if (gg)
		{
			astrncpy(groups[pid], gg, MAXGROUPLEN);
			log->Log(L_DRIVEL, "<capman> [%s] Player assigned to group '%s' from global staff list",
					pd->players[pid].name,
					groups[pid]);
		}
		else
			astrncpy(groups[pid], "default", MAXGROUPLEN);
	}
	else
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
					LOGIT("global staff list (arena)");
				}
				else
				{
					/* it must not be 'arena:group', so use the whole
					 * thing */
					astrncpy(groups[pid], gg, MAXGROUPLEN);
					LOGIT("global staff list (global)");
				}
			}
			else
			{
				/* this must be a group valid everywhere, so it takes
				 * precedence */
				astrncpy(groups[pid], gg, MAXGROUPLEN);
				LOGIT("global staff list (global)");
			}
		}
		else if (ag)
		{
			/* use arena-assigned group */
			astrncpy(groups[pid], ag, MAXGROUPLEN);
			LOGIT("arena staff list");
		}
		else /* just give him the default */
			astrncpy(groups[pid], "default", MAXGROUPLEN);
	}
#undef LOGIT
}


void PlayerAction(int pid, int action, int arena)
{
	if (action == PA_PREENTERARENA)
		UpdateGroup(pid, arena);
	else if (action == PA_CONNECT)
		UpdateGroup(pid, -1);
	else if (action == PA_DISCONNECT || action == PA_LEAVEARENA)
		astrncpy(groups[pid], "none", MAXGROUPLEN);
}


char *GetGroup(int pid)
{
	return groups[pid];
}


void SetGroup(int pid, const char *group)
{
	if (group)
		astrncpy(groups[pid], group, MAXGROUPLEN);
}


int HasCapability(int pid, const char *cap)
{
	if (cfg->GetStr(groupdef, groups[pid], cap))
		return 1;
	else
		return 0;
}


