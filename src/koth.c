
/* dist: public */

#include <stdlib.h>

#include "asss.h"
#include "koth.h"
#include "packets/koth.h"

/* King:DeathCount:::Number of deaths a player is allowed until his
 * crown is removed
 * King:ExpireTime:::Initial time given to each player at beginning of
 * 'King of the Hill' round
 * King:RewardFactor:::Number of points given to winner of 'King of the
 * Hill' round (uses FlagReward formula)
 * King:NonCrownAdjustTime:::Amount of time added for killing a player
 * without a crown
 * King:NonCrownMinimumBounty:::Minimum amount of bounty a player must
 * have in order to receive the extra time.
 * King:CrownRecoverKills:::Number of crown kills a non-crown player
 * must get in order to get their crown back.
 */

struct koth_arena_data
{
	int deathcount, expiretime, /* killadjusttime, killminbty, */ recoverkills;
	int minplaying;
};

struct koth_player_data
{
	unsigned char crown, hadcrown, deaths, crownkills;
};


local struct koth_player_data pdata[MAXPLAYERS];
local int akey;

local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)


local Imodman *mm;
local Inet *net;
local Ichat *chat;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Icmdman *cmd;
local Ilogman *lm;
local Imainloop *ml;
local Istats *stats;



/* needs lock */
local void start_koth(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct S2CKoth pkt =
		{ S2C_KOTH, 1, adata->expiretime, -1 };

	int set[MAXPLAYERS+1], setc = 0, pid;

	pd->LockStatus();
	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (pd->players[pid].status == S_PLAYING &&
		    pd->players[pid].arena == arena)
		{
			if (pd->players[pid].shiptype != SPEC)
			{
				set[setc++] = pid;
				pdata[pid].crown = 1;
				SET_HAS_CROWN(pid);
				pdata[pid].hadcrown = 1;
				pdata[pid].deaths = 0;
				pdata[pid].crownkills = 0;
			}
			else
			{
				pdata[pid].crown = 0;
				UNSET_HAS_CROWN(pid);
				pdata[pid].hadcrown = 0;
			}
		}
	pd->UnlockStatus();
	set[setc] = -1;

	net->SendToSet(set, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	chat->SendArenaMessage(arena, "King of the Hill game starting");
	lm->LogA(L_DRIVEL, "koth", arena, "game starting");
}


/* needs lock */
local void check_koth(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	int pid, crowncount = 0;
	int playing = 0;
	int hadset[MAXPLAYERS+1], setc = 0;

	/* first count crowns and previous crowns. also count total playing
	 * players. also keep track of who had a crown. */
	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (pd->players[pid].status == S_PLAYING &&
		    pd->players[pid].arena == arena &&
		    pd->players[pid].shiptype != SPEC)
		{
			playing++;
			if (pdata[pid].crown)
				crowncount++;
			if (pdata[pid].hadcrown)
				hadset[setc++] = pid;
		}
	hadset[setc] = -1;

	/* figure out if there was a win */
	if (crowncount == 0 && setc > 0)
	{
		/* a bunch of people expired at once. reward them and then
		 * restart */

		int pts, *p;
		Ipoints_koth *pk = mm->GetInterface(I_POINTS_KOTH, arena);

		if (pk)
			pts = pk->GetPoints(arena, playing, setc);
		else
			pts = 1000 / setc;
		mm->ReleaseInterface(pk);

		for (p = hadset; *p != -1; p++)
		{
			stats->IncrementStat(*p, STAT_FLAG_POINTS, pts);
			stats->IncrementStat(*p, STAT_KOTH_GAMES_WON, 1);
			chat->SendArenaMessage(arena, "King of the Hill: %s awarded %d points",
					pd->players[*p].name, pts);
			lm->LogP(L_DRIVEL, "koth", *p, "won koth game");
		}
		stats->SendUpdates();

		if (playing >= adata->minplaying)
			start_koth(arena);
	}
	else if (crowncount == 0)
	{
		if (playing >= adata->minplaying)
			start_koth(arena);
	}

	/* now mark anyone without a crown as not having had a crown */
	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (pd->players[pid].status == S_PLAYING &&
		    pd->players[pid].arena == arena &&
		    pd->players[pid].shiptype != SPEC)
			pdata[pid].hadcrown = pdata[pid].crown;
}


local int timer(void *v)
{
	Arena *arena = v;
	LOCK();
	check_koth(arena);
	UNLOCK();
	return TRUE;
}

/* needs lock */
local void set_crown_time(int pid, int time)
{
	struct S2CKoth pkt =
		{ S2C_KOTH, 1, time, -1 };
	pdata[pid].crown = 1;
	SET_HAS_CROWN(pid);
	pdata[pid].hadcrown = 1;
	net->SendToOne(pid, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
}

/* needs lock */
local void remove_crown(int pid)
{
	Arena *arena = pd->players[pid].arena;
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct S2CKoth pkt =
		{ S2C_KOTH, 0, 0, pid };

	if (adata->expiretime == 0)
		return;

	pdata[pid].crown = 0;
	UNSET_HAS_CROWN(pid);
	net->SendToArena(arena, -1, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	lm->LogP(L_DRIVEL, "koth", pid, "lost crown");
}


local void load_settings(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	ConfigHandle ch = arena->cfg;

	LOCK();
	adata->deathcount = cfg->GetInt(ch, "King", "DeathCount", 0);
	adata->expiretime = cfg->GetInt(ch, "King", "ExpireTime", 18000);
	/*
	adata->killadjusttime = cfg->GetInt(ch, "King", "NonCrownAdjustTime", 1500);
	adata->killminbty = cfg->GetInt(ch, "King", "NonCrownMininumBounty", 0);
	*/
	adata->recoverkills = cfg->GetInt(ch, "King", "CrownRecoverKills", 0);
	adata->minplaying = cfg->GetInt(ch, "King", "MinPlaying", 3);
	UNLOCK();
}


local void paction(int pid, int action, Arena *arena)
{
	LOCK();
	pdata[pid].crown = pdata[pid].hadcrown = 0;
	UNSET_HAS_CROWN(pid);
	UNLOCK();
}


local void shipchange(int pid, int ship, int freq)
{
	if (ship == SPEC)
		paction(pid, 0, 0);
}


local void kill(Arena *arena, int killer, int killed, int bounty, int flags)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);

	LOCK();

	if (pdata[killed].crown)
	{
		pdata[killed].deaths++;
		if (pdata[killed].deaths > adata->deathcount)
			remove_crown(killed);
	}

	if (pdata[killer].crown)
	{
		/* doesn't work now:
		if (!pdata[killed].crown && bounty >= adata->killminbty)
			add_crown_time(killer, adata->killadjusttime);
			 */
		set_crown_time(killer, adata->expiretime);
	}
	else
	{
		/* no crown. if the killed does, count this one */
		if (pdata[killed].crown && adata->recoverkills > 0)
		{
			int left;

			pdata[killer].crownkills++;
			left = adata->recoverkills - pdata[killer].crownkills;

			if (left <= 0)
			{
				pdata[killer].crownkills = 0;
				pdata[killer].deaths = 0;
				set_crown_time(killer, adata->expiretime);
				chat->SendMessage(killer, "You earned back a crown");
				lm->LogP(L_DRIVEL, "koth", killer, "earned back a crown");
			}
			else
				chat->SendMessage(killer, "%d kill%s left to earn back a crown",
						left, left == 1 ? "" : "s");
		}
	}

	UNLOCK();
}


local void p_kothexired(int pid, byte *p, int l)
{
	LOCK();
	remove_crown(pid);
	UNLOCK();
}


local void Cresetkoth(const char *params, int pid, const Target *t)
{
	Arena *arena = pd->players[pid].arena;
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	/* check to be sure koth is even running in this arena */
	LOCK();
	if (adata->expiretime)
		start_koth(arena);
	UNLOCK();
}



EXPORT int MM_koth(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		net = mm->GetInterface(I_NET, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		if (!net || !chat || !pd || !aman || !cfg || !cmd || !lm || !ml || !stats)
			return MM_FAIL;

		akey = aman->AllocateArenaData(sizeof(struct koth_arena_data));
		if (akey == -1) return MM_FAIL;

		cmd->AddCommand("resetkoth", Cresetkoth, NULL);

		net->AddPacket(C2S_KOTHEXPIRED, p_kothexired);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("resetkoth", Cresetkoth);
		net->RemovePacket(C2S_KOTHEXPIRED, p_kothexired);
		ml->ClearTimer(timer, NULL);

		aman->FreeArenaData(akey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		load_settings(arena);
		mm->RegCallback(CB_PLAYERACTION, paction, arena);
		mm->RegCallback(CB_SHIPCHANGE, shipchange, arena);
		mm->RegCallback(CB_KILL, kill, arena);
		ml->SetTimer(timer, 500, 500, (void*)arena, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
		adata->expiretime = 0;
		mm->UnregCallback(CB_PLAYERACTION, paction, arena);
		mm->UnregCallback(CB_SHIPCHANGE, shipchange, arena);
		mm->UnregCallback(CB_KILL, kill, arena);
		ml->ClearTimer(timer, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

