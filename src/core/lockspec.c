
/* dist: public */

#include <stdio.h>

#include "asss.h"

local int GetAllowableShips(Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	if (err_buf)
	{
		snprintf(err_buf, buf_len, "This arena does not allow players to leave spectator mode.");
	}

	return 0; // allow only spec
}

local int CanChangeFreq(Player *p, int new_freq, char *err_buf, int buf_len)
{
	return 1; // don't care about freq
}

local Ienforcer my_int =
{
	INTERFACE_HEAD_INIT(I_ENFORCER, "lockspec")
	GetAllowableShips, CanChangeFreq
};

EXPORT int MM_lockspec(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&my_int, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&my_int, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

