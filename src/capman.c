
/* dist: public */

#include <string.h>

#include "asss.h"

typedef struct
{
	char group[MAXGROUPLEN];
} pdata;

/* data */
local ConfigHandle groupdef, gstaff;
local int pdkey;

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ilogman *lm;
local Iconfig *cfg;


local void UpdateGroup(Player *p, Arena *arena)
{
#define LOGIT(from) \
	lm->Log(L_DRIVEL, "<capman> {%s} [%s] Player assigned to group '%s' from %s", \
			arena->name, \
			p->name, \
			group, \
			from)

	char *group = ((pdata*)PPDATA(p, pdkey))->group;

	if (!arena)
	{
		/* only global groups available for now */
		const char *gg = cfg->GetStr(gstaff, "Staff", p->name);
		if (gg)
		{
			astrncpy(group, gg, MAXGROUPLEN);
			lm->Log(L_DRIVEL, "<capman> [%s] Player assigned to group '%s' from global staff list",
					p->name,
					group);
		}
		else
			astrncpy(group, "default", MAXGROUPLEN);
	}
	else
	{
		const char *gg = cfg->GetStr(gstaff, "Staff", p->name);
		const char *ag = cfg->GetStr(arena->cfg, "Staff", p->name);

		if (gg)
		{
			char *t;
			/* check if this is an 'arena:group' thing */
			t = strstr(gg, arena->name);
			if (t)
			{
				t = strchr(t, ':');
				if (t)
				{
					int pos = 0;
					t++; /* skip ':' */
					while (pos < MAXGROUPLEN && *t && *t != ' ' && *t != ',')
						group[pos++] = *t++;
					LOGIT("global staff list (arena)");
				}
				else
				{
					/* it must not be 'arena:group', so use the whole
					 * thing */
					astrncpy(group, gg, MAXGROUPLEN);
					LOGIT("global staff list (global)");
				}
			}
			else
			{
				/* this must be a group valid everywhere, so it takes
				 * precedence */
				astrncpy(group, gg, MAXGROUPLEN);
				LOGIT("global staff list (global)");
			}
		}
		else if (ag)
		{
			/* use arena-assigned group */
			astrncpy(group, ag, MAXGROUPLEN);
			LOGIT("arena staff list");
		}
		else /* just give him the default */
			astrncpy(group, "default", MAXGROUPLEN);
	}
#undef LOGIT
}


local void PlayerAction(Player *p, int action, Arena *arena)
{
	char *group = ((pdata*)PPDATA(p, pdkey))->group;
	if (action == PA_PREENTERARENA)
		UpdateGroup(p, arena);
	else if (action == PA_CONNECT)
		UpdateGroup(p, NULL);
	else if (action == PA_DISCONNECT || action == PA_LEAVEARENA)
		astrncpy(group, "none", MAXGROUPLEN);
}


local const char *GetGroup(Player *p)
{
	char *group = ((pdata*)PPDATA(p, pdkey))->group;
	return group;
}


local void SetTempGroup(Player *p, const char *newgroup)
{
	char *group = ((pdata*)PPDATA(p, pdkey))->group;
	if (newgroup)
		astrncpy(group, newgroup, MAXGROUPLEN);
}


local void SetPermGroup(Player *p, const char *group, int global, const char *info)
{
	ConfigHandle ch;
	Arena *arena = p->arena;

	/* figure out where to set it */
	ch = global ? gstaff : (arena ? arena->cfg : NULL);
	if (!ch) return;

	/* first set it for the current session */
	SetTempGroup(p, group);

	/* now set it permanently */
	cfg->SetStr(ch, "Staff", p->name, group, info);
}

local int CheckGroupPassword(const char *group, const char *pw)
{
	const char *correctpw;
	correctpw = cfg->GetStr(gstaff, "Groups", group);
	return correctpw ? (strcmp(correctpw, pw) == 0) : 0;
}


local int HasCapability(Player *p, const char *cap)
{
	char *group = ((pdata*)PPDATA(p, pdkey))->group;
	if (cfg->GetStr(groupdef, group, cap))
		return 1;
	else
		return 0;
}


local int HasCapabilityByName(const char *name, const char *cap)
{
	/* figure out his group */
	const char *group;

	group = cfg->GetStr(gstaff, "Staff", name);
	if (!group)
		group = "default";

	if (cfg->GetStr(groupdef, group, cap))
		return 1;
	else
		return 0;
}


/* interface */

local Icapman capint =
{
	INTERFACE_HEAD_INIT(I_CAPMAN, "capman-groups")
	HasCapability, HasCapabilityByName
};

local Igroupman grpint =
{
	INTERFACE_HEAD_INIT(I_GROUPMAN, "groupman")
	GetGroup, SetPermGroup, SetTempGroup,
	CheckGroupPassword
};


EXPORT int MM_capman(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!pd || !aman || !lm || !cfg) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);

		groupdef = cfg->OpenConfigFile(NULL, "groupdef.conf", NULL, NULL);
		gstaff = cfg->OpenConfigFile(NULL, "staff.conf", NULL, NULL);

		mm->RegInterface(&capint, ALLARENAS);
		mm->RegInterface(&grpint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&capint, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&grpint, ALLARENAS))
			return MM_FAIL;
		cfg->CloseConfigFile(groupdef);
		cfg->CloseConfigFile(gstaff);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


