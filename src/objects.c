
/* dist: public */

#include <stdlib.h>
#include <string.h>

#include "asss.h"
#include "objects.h"

#include "packets/objects.h"

/* command funcs */
local void Cobjon(const char *params, Player *p, const Target *target);
local void Cobjoff(const char *params, Player *p, const Target *target);
local void Cobjset(const char *params, Player *p, const Target *target);

/* interface funcs */
local void ToggleArenaMultiObjects(Arena *arena, short *objs, char *ons, int size);
local void TogglePidSetMultiObjects(LinkedList *set, short *objs, char *ons, int size);
local void ToggleMultiObjects(Player *p, short *objs, char *ons, int size);
local void ToggleArenaObject(Arena *arena, short obj, char on);
local void ToggleObject(Player *p, short obj, char on);

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


EXPORT int MM_objects(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
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

void Cobjon(const char *params, Player *p, const Target *target)
{
	LinkedList set = LL_INITIALIZER;
	short obj = atoi(params);
	char on = 1;

	pd->TargetToSet(target, &set);
	TogglePidSetMultiObjects(&set, &obj, &on, 1);
	LLEmpty(&set);
}

void Cobjoff(const char *params, Player *p, const Target *target)
{
	LinkedList set = LL_INITIALIZER;
	short obj = atoi(params);
	char on = 0;

	pd->TargetToSet(target, &set);
	TogglePidSetMultiObjects(&set, &obj, &on, 1);
	LLEmpty(&set);
}

void Cobjset(const char *params, Player *p, const Target *target)
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
		LinkedList set = LL_INITIALIZER;
		pd->TargetToSet(target, &set);
		TogglePidSetMultiObjects(&set, objs, ons, l);
		LLEmpty(&set);
	}
}

void ToggleArenaObject(Arena *arena, short obj, char on)
{
	ToggleArenaMultiObjects(arena, &obj, &on, 1);
}

void ToggleObject(Player *p, short obj, char on)
{
	ToggleMultiObjects(p, &obj, &on, 1);
}

void ToggleArenaMultiObjects(Arena *arena, short *objs, char *ons, int size)
{
	LinkedList set = LL_INITIALIZER;
	Target targ = { T_ARENA };
	targ.u.arena = arena;
	pd->TargetToSet(&targ, &set);
	TogglePidSetMultiObjects(&set, objs, ons, size);
	LLEmpty(&set);
}

void ToggleMultiObjects(Player *p, short *objs, char *ons, int size)
{
	Link l = { NULL, p };
	LinkedList set = { &l, &l };
	TogglePidSetMultiObjects(&set, objs, ons, size);
}

void TogglePidSetMultiObjects(LinkedList *set, short *objs, char *ons, int size)
{
	struct ObjectToggling *pkt;
	int c;

	if (size < 1)
		return;

	pkt = alloca(1 + 2 * size);
	pkt->type = S2C_TOGGLEOBJ;

	for (c = 0; c < size; c++)
		pkt->objs[c] = ons[c] ? objs[c] : objs[c] | 0x8000;

	net->SendToSet(set, (byte*)pkt, 1 + 2 * size, NET_RELIABLE);
}

