
#include "asss.h"


struct TurretData
{
	int pid, endtime, interval, tosend;
	struct C2SPosition pos;
};


local LinkedList turrets;
local pthread_mutex_t turret_mtx = PTHREAD_MUTEX_INITIALIZER;

local Imodman *mm;
local Iplayerdata *pd;
local Icmdman *cmd;
local Ifake *fake;


local struct TurretData * new_turret(int pid, int timeout, int interval, int pid_for_position)
{
	struct C2SPosition *pos;
	struct PlayerPosition *src = &pd->players[pid_for_position].position;
	struct TurretData *td = amalloc(sizeof(*td));

	td->pid = pid;
	td->endtime = GTC() + timeout;
	td->interval = interval;
	td->tosend = 0;

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
	pos->weapon.type = W_PROXBOMB;
	pos->weapon.level = 0;
	pos->weapon.shraplevel = 0;
	pos->weapon.shrap = 0;
	pos->weapon.alternate = 0;

	pthread_mutex_lock(&turret_mtx);
	LLAdd(&turrets, td);
	pthread_mutex_unlock(&turret_mtx);
	return td;
}


local void Cdropturret(const char *params, int pid, int target)
{
	int tpid;

	tpid = fake->CreateFakePlayer(
			"<autoturret>",
			pd->players[pid].arena,
			WARBIRD,
			pd->players[pid].freq,
			NULL);
	new_turret(tpid, 1500, 150, pid);
}


local void checksum(struct C2SPosition *pkt, int n)
{
	int i = n;
	u8 ck = 0, *p = (u8*)pkt;
	pkt->checksum = 0;
	while (i--)
		ck ^= *p++;
	pkt->checksum = ck;
}

local void mlfunc()
{
	unsigned now;
	struct TurretData *td;
	Link *l, *next;

	pthread_mutex_lock(&turret_mtx);
	for (l = LLGetHead(&turrets); l; l = next)
	{
		td = l->data;
		next = l->next; /* so we can remove during the loop */
		now = GTC();
		if (now > td->endtime)
		{
			/* remove it from the list, kill the turret, and free the
			 * memory */
			LLRemove(&turrets, td);
			fake->EndFaked(td->pid);
			afree(td);
		}
		else if (now > td->tosend)
		{
			td->tosend = now + td->interval;
			td->pos.bounty = (td->endtime - now) / 100;
			td->pos.time = now;
			checksum(&td->pos, 22);
			fake->ProcessPacket(td->pid, (byte*)&td->pos, 22);
		}
	}
	pthread_mutex_unlock(&turret_mtx);
}


EXPORT int MM_autoturret(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		if (!pd || !cmd || !fake) return MM_FAIL;
		LLInit(&turrets);
		mm->RegCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		cmd->AddCommand("dropturret", Cdropturret);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("dropturret", Cdropturret);
		mm->UnregCallback(CB_MAINLOOP, mlfunc, ALLARENAS);
		LLEmpty(&turrets);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(fake);
		return MM_OK;
	}
	return MM_FAIL;
}


