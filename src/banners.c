
#include "asss.h"

#include "packets/billmisc.h"
#include "packets/banners.h"

local Imodman *mm;
local Inet *net;
local Ibillcore *bnet;
local Iplayerdata *pd;
local Ilogman *lm;

/* big array of all player's banners (24k) */
/* the player status mutex protects these. stupid but easy. */
local Banner banners[MAXPLAYERS];
/* 0 = no banner present, 1 = present but not used, 2 = present and used */
local byte banner_status[MAXPLAYERS];
/* this way you can turn the banner on when they    *
 * pass the CheckBanner test.                       *
 * NOTE: If CheckBanner relies on points or some    *
 *       other module, this will need to register   *
 *       callbacks with that module so that it can  *
 *       check if the player has his banner showing *
 *       and if not but he passes CheckBanner now,  *
 *       then send out his banner.                  *
 *       Effectively, this is here so that people   *
 *       don't have to re-enter arena in order that *
 *       their banner be displayed to the arena     */


/* sends pid's banner to arena he's in. you probably want to be holding
 * the player status lock when calling this. */
local void BannerToArena(int pid)
{
	struct S2CBanner send;

	if (banner_status[MAXPLAYERS] != 2 || ARENA_BAD(pd->players[pid].arena))
		return;
	send.type = S2C_BANNER;
	send.pid = pid;
	send.banner = banners[pid];

	net->SendToArena(pd->players[pid].arena, -1, (byte*)&send, sizeof(send),
			NET_RELIABLE | NET_PRI_N1);
}

/* return true to allow banner */
local int CheckBanner(int pid)
{
	/* FIXME: either put more logic in here, or have it call out to some
	 * interface to figure out if this player can have a banner. */
	/* for now, everyone can have banners. */
	return TRUE;
}


local void PBanner(int pid, byte *p, int len)
{
	struct C2SBanner *b = (struct C2SBanner*)p;

	if (len != sizeof(*b))
	{
		if (lm) lm->LogP(L_MALICIOUS, "banners", pid, "Bad size for banner packet");
		return;
	}

	/* grab the banner */
	pd->LockStatus();
	banners[pid] = b->banner;
	banner_status[pid] = 1;
	/* check if he can have one */
	if (CheckBanner(pid))
	{
		banner_status[pid] = 2;
		/* send to everyone */
		BannerToArena(pid);
		/* send to biller */
		if (bnet)
		{
			struct S2BBanner bsend;
			bsend.type = S2B_BANNER;
			bsend.pid = pid;
			bsend.banner = b->banner;
			bnet->SendToBiller((byte*)&bsend, sizeof(bsend), NET_RELIABLE);
		}

		if (lm) lm->Log(L_DRIVEL, "<banners> [%s] Recvd banner", pd->players[pid].name);
	}
	else
	{
		if (lm) lm->LogP(L_INFO, "banners", pid, "denied permission to use a banner");
	}
	pd->UnlockStatus();
}


local void BBanner(int bpid, byte *p, int len)
{
	struct B2SPlayerResponse *r = (struct B2SPlayerResponse *)p;
	int pid = r->pid;

	if (len != sizeof(*r))
	{
		if (lm) lm->Log(L_MALICIOUS, "banners", pid, "Bad size for billing player data packet");
		return;
	}

	/* grab the banner */
	pd->LockStatus();
	banners[pid] = r->banner;
	banner_status[pid] = 1;
	pd->UnlockStatus();

	/* don't bother sending it here because the player isn't in an arena
	 * yet */
}


local void PA(int pid, int action, int arena)
{
	if (action == PA_CONNECT || action == PA_DISCONNECT)
		/* reset banner */
		banner_status[pid] = 0;
	else if (action == PA_ENTERARENA)
	{
		int i, arena;

		pd->LockStatus();

		/* first check permissions on a stored banner from the biller
		 * and send it to the arena. */
		if (banner_status[pid] == 1 && CheckBanner(pid))
		{
			banner_status[pid] = 2;
			BannerToArena(pid);
		}

		/* then send everyone else's banner to him */
		arena = pd->players[pid].arena;
		for (i = 0; i < MAXPLAYERS; i++)
			if (pd->players[i].status == S_PLAYING &&
			    pd->players[i].arena == arena &&
			    banner_status[i] == 2 &&
			    i != pid)
			{
				struct S2CBanner send = { S2C_BANNER, pid };
				send.banner = banners[i];
				net->SendToOne(pid, (byte*)&send, sizeof(send), NET_RELIABLE | NET_PRI_N1);
			}
		pd->UnlockStatus();
	}
}


EXPORT int MM_banners(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		bnet = mm->GetInterface(I_BILLCORE, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!net || !pd) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PA, ALLARENAS);
		net->AddPacket(C2S_BANNER, PBanner);
		if (bnet) bnet->AddPacket(B2S_PLAYERDATA, BBanner);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_BANNER, PBanner);
		if (bnet) bnet->RemovePacket(B2S_PLAYERDATA, BBanner);
		mm->UnregCallback(CB_PLAYERACTION, PA, ALLARENAS);
		mm->ReleaseInterface(bnet);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

