
/* dist: public */

#include <stdlib.h>
#include <string.h>

#include "asss.h"
#include "objects.h"

#include "packets/objects.h"

/* command funcs */
local void Cobjon(const char *params, int pid, const Target *target);
local void Cobjoff(const char *params, int pid, const Target *target);
local void Cobjset(const char *params, int pid, const Target *target);

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

		cmd->AddCommand("objon", Cobjon, NULL);
		cmd->AddCommand("objoff", Cobjoff, NULL);
		cmd->AddCommand("objset", Cobjset, NULL);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("objon", Cobjon);
		cmd->RemoveCommand("objoff", Cobjoff);
		cmd->RemoveCommand("objset", Cobjset);

		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}

void Cobjon(const char *params, int pid, const Target *target)
{
	int set[MAXPLAYERS+1];
	short obj = atoi(params);
	char on = 1;

	pd->TargetToSet(target, set);
	TogglePidSetMultiObjects(set, &obj, &on, 1);
}

void Cobjoff(const char *params, int pid, const Target *target)
{
	int set[MAXPLAYERS+1];
	short obj = atoi(params);
	char on = 0;

	pd->TargetToSet(target, set);
	TogglePidSetMultiObjects(set, &obj, &on, 1);
}

void Cobjset(const char *params, int pid, const Target *target)
{
	int l = strlen(params) + 1;
	const char *c = params;
	short *objs = alloca(l * sizeof(short));
	char *ons = alloca(l * sizeof(char));

	l = 0;
	for (;;)
	{
		/* move to next + or - */
		while (*c != 0 && *c != '-' && *c != '+' && c[1] != '-' && c[1] != '+')
			c++;
		if (*c == 0)
			break;

		/* change it */
		if (*c == '+')
			ons[l] = 1;
		else
			ons[l] = 0;

		c++;
		objs[l++] = atoi(c);
	}

	if (l)
	{
		int set[MAXPLAYERS+1];
		pd->TargetToSet(target, set);
		TogglePidSetMultiObjects(set, objs, ons, l);
	}
}

void ToggleArenaObject(int arena, short obj, char on)
{
	ToggleArenaMultiObjects(arena, &obj, &on, 1);
}

void ToggleObject(int pid, short obj, char on)
{
	ToggleMultiObjects(pid, &obj, &on, 1);
}

void ToggleArenaMultiObjects(int arena, short *objs, char *ons, int size)
{
	int set[MAXPLAYERS+1];
	Target targ = { T_ARENA };
	targ.u.arena = arena;
	pd->TargetToSet(&targ, set);
	TogglePidSetMultiObjects(set, objs, ons, size);
}

void ToggleMultiObjects(int pid, short *objs, char *ons, int size)
{
	int set[2] = { pid, -1 };
	TogglePidSetMultiObjects(set, objs, ons, size);
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
}

