
#include "asss.h"


local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Inet *net;
local Icmdman *cmd;
local Ilogman *lm;


local int CreateFakePlayer(const char *name, int arena, int ship, int freq)
{
	int pid;
	PlayerData *player;

	/* create pid */
	pid = pd->NewPlayer(T_FAKE);
	if (PID_BAD(pid))
		return pid;
	player = pd->players + pid;

	/* set up playerdata struct and pretend he's logged in */
	astrncpy(player->name, name, 20);
	astrncpy(player->sendname, name, 20);
	astrncpy(player->squad, "", 20);
	astrncpy(player->sendsquad, "", 20);
	player->shiptype = ship;
	player->freq = freq;
	player->arena = player->oldarena = arena;

	/* enter arena */
	net->SendToArena(arena, pid, (byte*)player, 64, NET_RELIABLE);
	player->status = S_PLAYING;

	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] Fake player created",
				aman->arenas[arena].name,
				name);

	return pid;
}


local int EndFaked(int pid)
{
	int arena;
	struct SimplePacket pk = { S2C_PLAYERLEAVING };

	if (PID_BAD(pid))
		return 0;
	if (pd->players[pid].type != T_FAKE || pd->players[pid].status != S_PLAYING)
		return 0;

	arena = pd->players[pid].arena;
	pd->players[pid].arena = -1;

	/* leave arena */
	pk.d1 = pid;
	net->SendToArena(arena, pid, (byte*)&pk, 3, NET_RELIABLE);

	/* log before freeing pid to avoid races */
	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] Fake player destroyed",
				aman->arenas[arena].name,
				pd->players[pid].name);

	/* leave game */
	pd->players[pid].status = S_FREE;

	return 1;
}


local void Cmakefake(const char *params, int pid, const Target *target)
{
	CreateFakePlayer(params, pd->players[pid].arena, SPEC, 9999);
}


local void Ckillfake(const char *params, int pid, const Target *target)
{
	if (target->type == T_PID)
		EndFaked(target->u.pid);
}


local Ifake _int =
{
	INTERFACE_HEAD_INIT(I_FAKE, "fake")
	CreateFakePlayer, EndFaked
};


EXPORT int MM_fake(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!pd || !aman || !cmd || !net) return MM_FAIL;

		cmd->AddCommand("makefake", Cmakefake, NULL);
		cmd->AddCommand("killfake", Ckillfake, NULL);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(&_int, ALLARENAS);
		cmd->RemoveCommand("makefake", Cmakefake);
		cmd->RemoveCommand("killfake", Ckillfake);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


