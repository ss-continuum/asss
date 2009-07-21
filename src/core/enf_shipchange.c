
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"

typedef struct pdata
{
	int changes;
	ticks_t lastcheck;
} pdata;

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iplayerdata *pd;

local int pdkey;

local shipmask_t GetAllowableShips(Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	pdata *data = PPDATA(p, pdkey);
	int shipchangelimit;
	int d;

	/* cfghelp: General:ShipChangeLimit, global, int, def: 10
	 * The number of ship changes in a short time (about 10 seconds)
	 * before ship changing is disabled (for about 30 seconds). */
	shipchangelimit = cfg->GetInt(GLOBAL, "General", "ShipChangeLimit", 10);

	/* exponential decay by 1/2 every 10 seconds */
	d = TICK_DIFF(current_ticks(), data->lastcheck) / 1000;
	data->changes >>= d;
	data->lastcheck = TICK_MAKE(data->lastcheck + d * 1000);
	if (data->changes > shipchangelimit && shipchangelimit > 0)
	{
		lm->LogP(L_INFO, "game", p, "too many ship changes");
		/* disable for at least 30 seconds */
		data->changes |= (shipchangelimit<<3);
		if (err_buf)
			snprintf(err_buf, buf_len, "You're changing ships too often, disabling for 30 seconds.");
		if (p->p_ship != SHIP_SPEC)
			return SHIPMASK(p->p_ship);
		else
			return SHIPMASK_NONE;
	}
	data->changes++;

	return SHIPMASK_ALL;
}

local Aenforcer myadv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	GetAllowableShips, NULL
};

EXPORT int MM_enf_shipchange(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!lm || !cfg || !pd) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegAdviser(&myadv, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregAdviser(&myadv, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
