
#include <stdlib.h>

#include "asss.h"

#include "packets/objects.h"

/* command funcs */
local void Cobjon(const char *params, int pid, int target);
local void Cobjoff(const char *params, int pid, int target);

/* interface funcs */
local void ToggleArenaMultiObjects(int arena, short *objs, char *ons, int size);
local void TogglePidSetMultiObjects(int *pidset, short *objs, char *ons, int size);
local void ToggleMultiObjects(int pid, short *objs, char *ons, int size);
local void ToggleArenaObject(int arena, short obj, char on);
local void ToggleObject(int pid, short obj, char on);

/* local data */
local Imodman *mm;
local Iplayerdata *pd;
local Igame *game;
local Icmdman *cmd;
local Inet *net;

local Iobjects _myint =
{
	INTERFACE_HEAD_INIT(I_OBJECTS, "objects")
	ToggleArenaMultiObjects, TogglePidSetMultiObjects, ToggleMultiObjects,
	ToggleArenaObject, ToggleObject
};


EXPORT int MM_objects(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		net = mm->GetInterface(I_NET, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!cmd) return MM_FAIL;

		cmd->AddCommand("objon", Cobjon);
		cmd->AddCommand("objoff", Cobjoff);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("objon", Cobjon);
		cmd->RemoveCommand("objoff", Cobjoff);

		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}

void Cobjon(const char *params, int pid, int target)
{
	int arena = pd->players[pid].arena;

	if (PID_OK(target)) {
		if (pd->players[target].arena == pd->players[pid].arena)
			ToggleObject(target, (short)atoi(params), 1);
	}
	else
		ToggleArenaObject(arena, (short)atoi(params), 1);
}

void Cobjoff(const char *params, int pid, int target)
{
	int arena = pd->players[pid].arena;

	if (PID_OK(target)) {
		if (pd->players[target].arena == pd->players[pid].arena)
			ToggleObject(target, (short)atoi(params), 1);
	}
	else
		ToggleArenaObject(arena, (short)atoi(params), 1);
}

void ToggleArenaMultiObjects(int arena, short *objs, char *ons, int size)
{
	struct ObjectToggling *pkt;
	int c;

	if (size < 1 || ARENA_BAD(arena))
		return;

	pkt = alloca(1 + 2 * size);
	pkt->type = S2C_TOGGLEOBJ;

	for (c = 0; c < size; c++)
		pkt->objs[c] = ons[c] ? objs[c] | 0xF000 : objs[c];

	if (ARENA_OK(arena))
		net->SendToArena(arena, -1, (byte*)pkt, 1 + 2 * size, NET_RELIABLE);

	DO_CBS(CB_OBJECTTOGGLEARENA, arena, ObjectToggleArena,
			(arena, objs, ons, size));
}

void TogglePidSetMultiObjects(int *pidset, short *objs, char *ons, int size)
{
	struct ObjectToggling *pkt;
	int c;

	if (size < 1)
		return;

	pkt = alloca(1 + 2 * size);
	pkt->type = S2C_TOGGLEOBJ;

	for (c = 0; c < size; c++)
		pkt->objs[c] = ons[c] ? objs[c] | 0xF000 : objs[c];

	net->SendToSet(pidset, (byte*)pkt, 1 + 2 * size, NET_RELIABLE);

	for (c = 0; pidset[c] != -1; c++)
		DO_CBS(CB_OBJECTTOGGLEPID, pd->players[pidset[c]].arena, ObjectTogglePid,
				(pidset[c], objs, ons, size));
}

void ToggleMultiObjects(int pid, short *objs, char *ons, int size)
{
	struct ObjectToggling *pkt;
	int c;

	if (size < 1 || PID_BAD(pid))
		return;

	pkt = alloca(1 + 2 * size);
	pkt->type = S2C_TOGGLEOBJ;

	for (c = 0; c < size; c++)
		pkt->objs[c] = ons[c] ? objs[c] | 0xF000 : objs[c];

	net->SendToOne(pid, (byte*)pkt, 1 + 2 * size, NET_RELIABLE);
}

void ToggleArenaObject(int arena, short obj, char on)
{
	ToggleArenaMultiObjects(arena, &obj, &on, 1);
}

void ToggleObject(int pid, short obj, char on)
{
	ToggleMultiObjects(pid, &obj, &on, 1);
}

