
/* dist: public */

#include <string.h>

#include "asss.h"


local Iconfig *cfg;
local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmd;
local Istats *stats;
local Ichat *chat;
local Ilogman *lm;

local const struct
{
	const char *string;
	const char *setting;
	int type;
}
items[] =
{
	{ "x",        "XRadar",     6 },
	{ "recharge", "Recharge",   1 },
	{ "energy",   "Energy",     2 },
	{ "rot",      "Rotation",   3 },
	{ "stealth",  "Stealth",    4 },
	{ "cloak",    "Cloak",      5 },
	{ "gun",      "Gun",        8 },
	{ "bomb",     "Bomb",       9 },
	{ "bounce",   "Bounce",    10 },
	{ "thrust",   "Thrust",    11 },
	{ "speed",    "Speed",     12 },
	{ "multi",    "MultiFire", 15 },
	{ "prox",     "Prox",      16 },
	{ "super",    "Super",     17 },
	{ "shield",   "Shield",    18 },
	{ "shrap",    "Shrap",     19 },
	{ "anti",     "AntiWarp",  20 },
	{ "rep",      "Repel",     21 },
	{ "burst",    "Burst",     22 },
	{ "decoy",    "Decoy",     23 },
	{ "thor",     "Thor",      24 },
	{ "brick",    "Brick",     26 },
	{ "rocket",   "Rocket",    27 },
	{ "port",     "Portal",    28 },
};


local void print_costs(ConfigHandle ch, int pid)
{
	int i, avail = 0;

	for (i = 0; i < sizeof(items)/sizeof(items[0]); i++)
	{
		int cost = cfg->GetInt(ch, "Cost", items[i].setting, 0);
		if (cost)
		{
			chat->SendMessage(pid, "buy: %-9s %6d", items[i].setting, cost);
			avail++;
		}
	}

	if (avail == 0)
		chat->SendMessage(pid, "buy: There are no items available in this arena.");
}


local void Cbuy(const char *params, int pid, const Target *target)
{
	int arena = pd->players[pid].arena;
	ConfigHandle ch = aman->arenas[arena].cfg;
	int anywhere;

	if (ARENA_BAD(arena)) return;

	/* cfghelp: Cost:PurchaseAnytime, arena, bool, def: 0
	 * Whether players can buy items outside a safe zone. */
	anywhere = cfg->GetInt(ch, "Cost", "PurchaseAnytime", 0);

	if (pd->players[pid].shiptype == SPEC)
	{
		chat->SendMessage(pid, "Spectators cannot purchase items.");
		return;
	}

	if (!anywhere && !(pd->players[pid].position.status & 0x20))
	{
		chat->SendMessage(pid, "You must be in a safe zone to purchase items.");
		return;
	}

	if (params[0] == 0)
		print_costs(ch, pid);
	else
	{
		int i, item = -1;
		for (i = 0; i < sizeof(items)/sizeof(items[0]); i++)
			if (strstr(params, items[i].string))
				item = i;

		if (item == -1)
			chat->SendMessage(pid, "Invalid item specified for purchase.");
		else
		{
			int cost = cfg->GetInt(ch, "Cost", items[item].setting, 0);
			if (cost == 0)
				chat->SendMessage(pid, "That item isn't available for purchase.");
			else
			{
				int pts = stats->GetStat(pid, STAT_KILL_POINTS, INTERVAL_RESET) +
				          stats->GetStat(pid, STAT_FLAG_POINTS, INTERVAL_RESET);
				if (pts < cost)
					chat->SendMessage(pid, "You don't have enough points to purchase that item.");
				else
				{
					Target t;
					t.type = T_PID;
					t.u.pid = pid;
					/* deduct from flag points to keep kill average the same. */
					stats->IncrementStat(pid, STAT_FLAG_POINTS, -cost);
					game->GivePrize(&t, items[item].type, 1);
					chat->SendMessage(pid, "Bought %s.", items[item].setting);
					lm->LogP(L_DRIVEL, "buy", pid, "bought %s", items[item].setting);
				}
			}
		}
	}
}


EXPORT int MM_buy(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd->AddCommand("buy", Cbuy, NULL);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("buy", Cbuy);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

