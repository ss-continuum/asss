
/* dist: public */

#include "asss.h"
#include "billcore.h"

#include "packets/billmisc.h"
#include "packets/banners.h"

local Imodman *mm;
#if 0
local Ibillcore *bnet;
#endif
local Inet *net;
local Iplayerdata *pd;
local Ilogman *lm;

typedef struct
{
	Banner banner;
	/* 0 = no banner present, 1 = present but not used, 2 = present and used */
	byte status;
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
} bdata;

local int bdkey;


/* sends pid's banner to arena he's in. you probably want to be holding
 * the player status lock when calling this. */
local void BannerToArena(Player *p)
{
	bdata *bd = PPDATA(p, bdkey);
	struct S2CBanner send;

	if (bd->status != 2 || !p->arena)
		return;
	send.type = S2C_BANNER;
	send.pid = p->pid;
	send.banner = bd->banner;

	net->SendToArena(p->arena, NULL, (byte*)&send, sizeof(send),
			NET_RELIABLE | NET_PRI_N1);
}

/* return true to allow banner */
local int CheckBanner(Player *p)
{
	/* FIXME: either put more logic in here, or have it call out to some
	 * interface to figure out if this player can have a banner. */
	/* for now, everyone can have banners. */
	return TRUE;
}


local void PBanner(Player *p, byte *pkt, int len)
{
	bdata *bd = PPDATA(p, bdkey);
	struct C2SBanner *b = (struct C2SBanner*)pkt;
#if 0
	static unsigned lastbsend = 0;
	unsigned gtc = GTC();
#endif

	if (len != sizeof(*b))
	{
		if (lm) lm->LogP(L_MALICIOUS, "banners", p, "Bad size for banner packet");
		return;
	}

	/* grab the banner */
	bd->banner = b->banner;
	bd->status = 1;
	/* check if he can have one */
	if (CheckBanner(p))
	{
		bd->status = 2;

		/* send to everyone */
		BannerToArena(p);
#if 0
		/* send to biller */
		/* only allow 1 banner every .2 seconds to get sent to biller.
		 * any more get dropped */
		if (bnet && (gtc - lastbsend) > 20)
		{
			struct S2BBanner bsend;
			bsend.type = S2B_BANNER;
			bsend.pid = p->pid;
			bsend.banner = b->banner;
			bnet->SendToBiller((byte*)&bsend, sizeof(bsend),
					NET_RELIABLE | NET_PRI_N1);
			lastbsend = gtc;
		}
#endif

		if (lm) lm->Log(L_DRIVEL, "<banners> [%s] Recvd banner", p->name);
	}
	else
	{
		if (lm) lm->LogP(L_INFO, "banners", p, "denied permission to use a banner");
	}
}


#if 0
local void BBanner(int bpid, byte *p, int len)
{
	struct B2SPlayerResponse *r = (struct B2SPlayerResponse *)p;
	Player *p = r->pid;

	if (len != sizeof(*r))
	{
		if (lm) lm->Log(L_MALICIOUS, "banners", pid, "Bad size for billing player data packet");
		return;
	}

	/* grab the banner */
	pd->LockStatus();
	bd->banner = r->banner;
	bd->status = 1;
	pd->UnlockStatus();

	/* don't bother sending it here because the player isn't in an arena
	 * yet */
}
#endif


local void PA(Player *p, int action, Arena *arena)
{
	bdata *bd = PPDATA(p, bdkey), *ibd;

	if (action == PA_CONNECT || action == PA_DISCONNECT)
		/* reset banner */
		bd->status = 0;
	else if (action == PA_ENTERARENA)
	{
		Link *link;
		Player *i;

		/* first check permissions on a stored banner from the biller
		 * and send it to the arena. */
		if (bd->status == 1 && CheckBanner(p))
		{
			bd->status = 2;
			BannerToArena(p);
		}

		/* then send everyone's banner to him */
		pd->Lock();
		FOR_EACH_PLAYER_P(i, ibd, bdkey)
			if (i->status == S_PLAYING &&
			    i->arena == arena &&
			    ibd->status == 2)
			{
				struct S2CBanner send = { S2C_BANNER, i->pid };
				send.banner = ibd->banner;
				net->SendToOne(p, (byte*)&send, sizeof(send), NET_RELIABLE | NET_PRI_N1);
			}
		pd->Unlock();
	}
}


EXPORT int MM_banners(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
#if 0
		bnet = mm->GetInterface(I_BILLCORE, ALLARENAS);
		if (bnet) bnet->AddPacket(B2S_PLAYERDATA, BBanner);
#endif
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!net || !pd) return MM_FAIL;

		bdkey = pd->AllocatePlayerData(sizeof(bdata));
		if (bdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PA, ALLARENAS);
		net->AddPacket(C2S_BANNER, PBanner);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_BANNER, PBanner);
		mm->UnregCallback(CB_PLAYERACTION, PA, ALLARENAS);
		pd->FreePlayerData(bdkey);
#if 0
		if (bnet) bnet->RemovePacket(B2S_PLAYERDATA, BBanner);
		mm->ReleaseInterface(bnet);
#endif
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

