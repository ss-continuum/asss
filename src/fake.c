
/* dist: public */

#include "asss.h"
#include "fake.h"

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Inet *net;
local Ichatnet *chatnet;
local Icmdman *cmd;
local Ilogman *lm;


local Player * CreateFakePlayer(const char *name, Arena *arena, int ship, int freq)
{
	Player *p;

	/* create pid */
	p = pd->NewPlayer(T_FAKE);
	if (!p) return NULL;

	/* set up pdata struct and pretend he's logged in */
	astrncpy(p->name, name, 20);
	astrncpy(p->pkt.name, name, 20);
	astrncpy(p->squad, "", 20);
	astrncpy(p->pkt.squad, "", 20);
	p->p_ship = ship;
	p->p_freq = freq;
	p->arena = p->oldarena = arena;
	SET_SEND_DAMAGE(p);

	/* enter arena */
	if (net) net->SendToArena(arena, p, (byte*)&p->pkt, 64, NET_RELIABLE);
	if (chatnet) chatnet->SendToArena(arena, p,
			"ENTERING:%s:%d:%d", name, ship, freq);
	p->status = S_PLAYING;

	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] Fake p created",
				arena->name,
				name);

	return p;
}


local int EndFaked(Player *p)
{
	Arena *arena;
	struct SimplePacket pk = { S2C_PLAYERLEAVING };

	if (!p)
		return 0;
	if (p->type != T_FAKE || p->status != S_PLAYING)
		return 0;

	arena = p->arena;
	p->arena = NULL;

	/* leave arena */
	pk.d1 = p->pid;
	if (net) net->SendToArena(arena, p, (byte*)&pk, 3, NET_RELIABLE);
	if (chatnet) chatnet->SendToArena(arena, p,
			"LEAVING:%s", p->name);

	/* log before freeing pid to avoid races */
	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] Fake player destroyed",
				arena->name,
				p->name);

	/* leave game */
	pd->FreePlayer(p);

	return 1;
}


local void Cmakefake(const char *params, Player *p, const Target *target)
{
	CreateFakePlayer(params, p->arena, SPEC, 9999);
}


local void Ckillfake(const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
		EndFaked(target->u.p);
}


local Ifake _int =
{
	INTERFACE_HEAD_INIT(I_FAKE, "fake")
	CreateFakePlayer, EndFaked
};


EXPORT int MM_fake(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!pd || !aman || !cmd) return MM_FAIL;

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
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}


