
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
	pid = net->NewConnection(T_FAKE, NULL);
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


local void Cmakefake(const char *params, int pid, int target)
{
	CreateFakePlayer(params, pd->players[pid].arena, SPEC, 9999);
}


local void Ckillfake(const char *params, int pid, int target)
{
	EndFaked(target);
}


local Ifake _int =
{
	INTERFACE_HEAD_INIT("fake")
	CreateFakePlayer, EndFaked, NULL
};


EXPORT int MM_fake(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface("playerdata", ALLARENAS);
		aman = mm->GetInterface("arenaman", ALLARENAS);
		cmd = mm->GetInterface("cmdman", ALLARENAS);
		net = mm->GetInterface("net", ALLARENAS);
		lm = mm->GetInterface("logman", ALLARENAS);

		if (!pd || !aman || !cmd || !net) return MM_FAIL;

		cmd->AddCommand("makefake", Cmakefake);
		cmd->AddCommand("killfake", Ckillfake);
		_int.ProcessPacket = net->ProcessPacket;
		mm->RegInterface("fake", &_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface("fake", &_int, ALLARENAS);
		cmd->RemoveCommand("makefake", Cmakefake);
		cmd->RemoveCommand("killfake", Ckillfake);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


