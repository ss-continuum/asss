
/* dist: public */

#include "asss.h"
#include "fake.h"


struct TurretData
{
	Player *p;
	int interval, weapon;
	ticks_t endtime, tofire, tosend;
	struct C2SPosition pos;
};


local LinkedList turrets;
local pthread_mutex_t turret_mtx = PTHREAD_MUTEX_INITIALIZER;

local Imodman *mm;
local Iplayerdata *pd;
local Icmdman *cmd;
local Igame *game;
local Ifake *fake;


local struct TurretData * new_turret(Player *p, int timeout, int interval, Player *p_for_position)
{
	struct C2SPosition *pos;
	struct PlayerPosition *src = &p_for_position->position;
	struct TurretData *td = amalloc(sizeof(*td));
	ticks_t gtc = current_ticks();

	td->p = p;
	td->endtime = gtc + timeout;
	td->interval = interval;
	td->tosend = gtc;
	td->tofire = gtc;

	pos = &td->pos;
	pos->type = C2S_POSITION;
	pos->rotation = src->rotation;
	pos->x = src->x;
	pos->y = src->y;
	pos->xspeed = pos->yspeed = 0;
	pos->status = 0;
	pos->bounty = 0;
	pos->energy = 1000;

	/* a default weapon. caller can fix this up. */
	td->weapon = W_PROXBOMB;
	pos->weapon.type = 0;
	pos->weapon.level = 0;
	pos->weapon.shraplevel = 0;
	pos->weapon.shrap = 0;
	pos->weapon.alternate = 0;

	pthread_mutex_lock(&turret_mtx);
	LLAdd(&turrets, td);
	pthread_mutex_unlock(&turret_mtx);
	return td;
}


local helptext_t dropturret_help =
"Module: autoturret\n"
"Targets: none\n"
"Args: none\n"
"Drops a turret right where your ship is. The turret will fire 10 level 1\n"
"bombs, 1.5 seconds apart, and then disappear.\n";

local void Cdropturret(const char *params, Player *p, const Target *target)
{
	Player *turret;

	turret = fake->CreateFakePlayer(
			"<autoturret>",
			p->arena,
			WARBIRD,
			p->p_freq);
	new_turret(turret, 1500, 150, p);
}


local void mlfunc()
{
	ticks_t now;
	struct TurretData *td;
	Link *l, *next;

	pthread_mutex_lock(&turret_mtx);
	for (l = LLGetHead(&turrets); l; l = next)
	{
		td = l->data;
		next = l->next; /* so we can remove during the loop */
		now = current_ticks();
		if (TICK_GT(now, td->endtime))
		{
			/* remove it from the list, kill the turret, and free the
			 * memory */
			LLRemove(&turrets, td);
			fake->EndFaked(td->p);
			afree(td);
		}
		else if (TICK_GT(now, td->tofire))
		{
			td->tofire = now + td->interval;
			td->tosend = now + 15;
			td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			td->pos.time = now;
			td->pos.weapon.type = td->weapon;
			td->pos.extra.energy++;
			game->FakePosition(td->p, &td->pos, sizeof(td->pos));
		}
		else if (TICK_GT(now, td->tosend))
		{
			td->tosend = now + 15;
			td->pos.bounty = TICK_DIFF(td->endtime, now) / 100;
			td->pos.time = now;
			td->pos.weapon.type = 0;
			td->pos.extra.energy++;
			game->FakePosition(td->p, &td->pos, sizeof(td->pos));
		}
	}
	pthread_mutex_unlock(&turret_mtx);
}


EXPORT int MM_autoturret(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		if (!pd || !cmd || !game || !fake) return MM_FAIL;
		LLInit(&turrets);
		mm->RegCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		cmd->AddCommand("dropturret", Cdropturret, dropturret_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("dropturret", Cdropturret);
		mm->UnregCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		LLEmpty(&turrets);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(fake);
		return MM_OK;
	}
	return MM_FAIL;
}


