
/* dist: public */

#include <time.h>
#include "asss.h"
#include "idle.h"

local Iplayerdata *pd;
local Inet *net;
local Ichatnet *chatnet;
local int pdkey;

local int GetIdle(Player *p)
{
	time_t *lastevt = PPDATA(p, pdkey), now = time(NULL);
	return now - *lastevt;
}

local void ResetIdle(Player *p)
{
	time(PPDATA(p, pdkey));
}

local void ppk(Player *p, byte *pkt, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)pkt;
	if (pos->weapon.type && (pos->time & 7) == 0)
		ResetIdle(p);
}

local void packetfunc(Player *p, byte *pkt, int len)
{
	ResetIdle(p);
}

local void messagefunc(Player *p, const char *line)
{
	ResetIdle(p);
}

local Iidle myint =
{
	INTERFACE_HEAD_INIT(I_IDLE, "idle")
	GetIdle, ResetIdle
};

EXPORT int MM_idle(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		if (!pd) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(time_t));
		if (pdkey == -1) return MM_FAIL;

		if (net)
		{
			net->AddPacket(C2S_GOTOARENA, packetfunc);
			net->AddPacket(C2S_CHAT, packetfunc);
			net->AddPacket(C2S_SPECREQUEST, packetfunc);
			net->AddPacket(C2S_SETFREQ, packetfunc);
			net->AddPacket(C2S_SETSHIP, packetfunc);
			net->AddPacket(C2S_BRICK, packetfunc);
			net->AddPacket(C2S_POSITION, ppk);
		}
		if (chatnet)
		{
			chatnet->AddHandler("GO", messagefunc);
			chatnet->AddHandler("CHANGEFREQ", messagefunc);
			chatnet->AddHandler("SEND", messagefunc);
		}

		mm->RegInterface(&myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myint, ALLARENAS))
			return MM_FAIL;
		if (net)
		{
			net->RemovePacket(C2S_GOTOARENA, packetfunc);
			net->RemovePacket(C2S_CHAT, packetfunc);
			net->RemovePacket(C2S_SPECREQUEST, packetfunc);
			net->RemovePacket(C2S_SETFREQ, packetfunc);
			net->RemovePacket(C2S_SETSHIP, packetfunc);
			net->RemovePacket(C2S_BRICK, packetfunc);
			net->RemovePacket(C2S_POSITION, ppk);
		}
		if (chatnet)
		{
			chatnet->RemoveHandler("GO", messagefunc);
			chatnet->RemoveHandler("CHANGEFREQ", messagefunc);
			chatnet->RemoveHandler("SEND", messagefunc);
		}
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		return MM_OK;
	}
	else
		return MM_FAIL;
}

