
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "filetrans.h"


struct transfer_data
{
	Player *from, *to;
	char clientpath[256];
	char fname[16];
};

local LinkedList offers;
local LinkedList transfers;

local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

local Ifiletrans *ft;
local Icmdman *cmd;
local Ichat *chat;
local Iplayerdata *pd;
local Ilogman *lm;


local int is_sending(Player *p)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->from == p)
			{ UNLOCK(); return 1; }
	for (l = LLGetHead(&transfers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->from == p)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local int is_recving(Player *p)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->to == p)
			{ UNLOCK(); return 1; }
	for (l = LLGetHead(&transfers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->to == p)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local void cancel_files(Player *p)
{
	Link *l, *n;
	LOCK();
	for (l = LLGetHead(&offers); l; l = n)
	{
		struct transfer_data *td = l->data;
		n = l->next;
		if (td->from == p || td->to == p)
		{
			afree(td);
			LLRemove(&offers, td);
		}
	}
	for (l = LLGetHead(&transfers); l; l = n)
	{
		struct transfer_data *td = l->data;
		n = l->next;
		if (td->from == p || td->to == p)
		{
			afree(td);
			LLRemove(&transfers, td);
		}
	}
	UNLOCK();
}


local void uploaded(Player *p, const char *path)
{
	Link *l;
	const char *t1, *t2;
	LOCK();
	for (l = LLGetHead(&transfers); l; l = l->next)
	{
		struct transfer_data *td = l->data;

		if (td->from == p)
		{
			LLRemove(&transfers, td);

			if (td->to->status != S_PLAYING || !IS_STANDARD(td->to))
			{
				lm->Log(L_WARN,
						"<sendfile> bad state or client type for recipient of received file");
				afree(td);
				goto error;
			}

			/* try to get basename of the client path */
			t1 = strrchr(td->clientpath, '/');
			t2 = strrchr(td->clientpath, '\\');
			if (t2 > t1) t1 = t2;
			t1 = t1 ? t1 + 1 : td->clientpath;

			if (ft->SendFile(td->to, path, t1, 1) != MM_OK)
				remove(path);

			afree(td);
			goto done;
		}
	}
	lm->Log(L_WARN, "<sendfile> can't file transfer for completed file upload");

error:
	remove(path);
done:
	UNLOCK();
}


local void Csendfile(const char *params, Player *p, const Target *target)
{
	struct transfer_data *td;
	Player *t = target->u.p;

	if (target->type != T_PLAYER) return;

	if (!*params) return;

	if (is_sending(p))
	{
		chat->SendMessage(p, "You are currently sending a file");
		return;
	}

	if (is_recving(t))
	{
		chat->SendMessage(p, "That player is currently receiving a file");
		return;
	}

	if (p->p_ship != SPEC)
	{
		chat->SendMessage(p, "You must be in spectator mode to offer files");
		return;
	}

	if (t->p_ship != SPEC)
	{
		chat->SendMessage(p, "You must offer files to another player in spectator mode");
		return;
	}

	td = amalloc(sizeof(*td));
	td->from = p;
	td->to = t;
	astrncpy(td->clientpath, params, sizeof(td->clientpath));
	astrncpy(td->fname, "c2c-XXXXXX", sizeof(td->fname));
	mktemp(td->fname);

	chat->SendMessage(t, "%s wants to send you the file \"%s\". To accept type ?acceptfile.",
			p->name, params);
	LOCK();
	LLAdd(&offers, td);
	UNLOCK();
}


local void Ccancelfile(const char *params, Player *p, const Target *target)
{
	cancel_files(p);
	chat->SendMessage(p, "Your file offers have been cancelled");
}


local void Cacceptfile(const char *params, Player *p, const Target *t)
{
	Link *l;

	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
	{
		struct transfer_data *td = l->data;
		if (td->to == p)
		{
			ft->RequestFile(td->from, td->clientpath, td->fname);
			LLRemove(&offers, td);
			LLAdd(&transfers, td);
			goto done;
		}
	}
	chat->SendMessage(p, "Nobody has offered any files to you.");
done:
	UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_CONNECT || action == PA_DISCONNECT)
		cancel_files(p);
}


EXPORT int MM_sendfile(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		ft = mm->GetInterface(I_FILETRANS, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!ft || !cmd || !chat || !pd || !lm)
			return MM_FAIL;

		cmd->AddCommand("sendfile", Csendfile, NULL);
		cmd->AddCommand("acceptfile", Cacceptfile, NULL);
		cmd->AddCommand("cancelfile", Ccancelfile, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_UPLOADEDFILE, uploaded, ALLARENAS);

		LLInit(&transfers);
		LLInit(&offers);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_UPLOADEDFILE, uploaded, ALLARENAS);
		cmd->RemoveCommand("sendfile", Csendfile);
		cmd->RemoveCommand("acceptfile", Cacceptfile);
		cmd->RemoveCommand("cancelfile", Ccancelfile);
		LLEnum(&transfers, afree);
		LLEmpty(&transfers);
		LLEnum(&offers, afree);
		LLEmpty(&offers);
		mm->ReleaseInterface(ft);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

