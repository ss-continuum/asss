
#include "asss.h"

local Iarenaman *aman;
local Iconfig *cfg;

local int getpoints(int arena, int freq, int freqplayers, int totalplayers, int flagsowned)
{
	int rwpts = cfg->GetInt(aman->arenas[arena].cfg, "Periodic", "RewardPoints", 100);
	if (rwpts > 0)
		return flagsowned * rwpts;
	else
		return flagsowned * (-rwpts) * totalplayers;
}

local Iperiodicpoints myint =
{
	INTERFACE_HEAD_INIT(I_PERIODIC_POINTS, "pp-basic")
	getpoints
};

EXPORT int MM_points_periodic(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!aman || !cfg) return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&myint, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&myint, arena);
		return MM_OK;
	}
	return MM_FAIL;
}
