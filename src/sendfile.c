
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "filetrans.h"


struct transfer_data
{
	int frompid, topid;
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


local int is_sending(int pid)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->frompid == pid)
			{ UNLOCK(); return 1; }
	for (l = LLGetHead(&transfers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->frompid == pid)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local int is_recving(int pid)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->topid == pid)
			{ UNLOCK(); return 1; }
	for (l = LLGetHead(&transfers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->topid == pid)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local void cancel_files(int pid)
{
	Link *l, *n;
	LOCK();
	for (l = LLGetHead(&offers); l; l = n)
	{
		struct transfer_data *td = l->data;
		n = l->next;
		if (td->frompid == pid || td->topid == pid)
		{
			afree(td);
			LLRemove(&offers, td);
		}
	}
	for (l = LLGetHead(&transfers); l; l = n)
	{
		struct transfer_data *td = l->data;
		n = l->next;
		if (td->frompid == pid || td->topid == pid)
		{
			afree(td);
			LLRemove(&transfers, td);
		}
	}
	UNLOCK();
}


local void uploaded(int pid, const char *path)
{
	Link *l;
	const char *t1, *t2;
	LOCK();
	for (l = LLGetHead(&transfers); l; l = l->next)
	{
		struct transfer_data *td = l->data;

		if (td->frompid == pid)
		{
			LLRemove(&transfers, td);

			if (pd->players[td->topid].status != S_PLAYING || !IS_STANDARD(td->topid))
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

			if (ft->SendFile(td->topid, path, t1, 1) != MM_OK)
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


local void Csendfile(const char *params, int pid, const Target *target)
{
	struct transfer_data *td;
	int t = target->u.pid;

	if (target->type != T_PID) return;

	if (!*params) return;

	if (is_sending(pid))
	{
		chat->SendMessage(pid, "You are currently sending a file");
		return;
	}

	if (is_recving(t))
	{
		chat->SendMessage(pid, "That player is currently receiving a file");
		return;
	}

	if (pd->players[pid].shiptype != SPEC)
	{
		chat->SendMessage(pid, "You must be in spectator mode to offer files");
		return;
	}

	if (pd->players[t].shiptype != SPEC)
	{
		chat->SendMessage(pid, "You must offer files to another player in spectator mode");
		return;
	}

	td = amalloc(sizeof(*td));
	td->frompid = pid;
	td->topid = t;
	astrncpy(td->clientpath, params, sizeof(td->clientpath));
	astrncpy(td->fname, "c2c-XXXXXX", sizeof(td->fname));
	mktemp(td->fname);

	chat->SendMessage(t, "%s wants to send you the file \"%s\". To accept type ?acceptfile.",
			pd->players[pid].name, params);
	LOCK();
	LLAdd(&offers, td);
	UNLOCK();
}


local void Ccancelfile(const char *params, int pid, const Target *target)
{
	cancel_files(pid);
	chat->SendMessage(pid, "Your file offers have been cancelled");
}


local void Cacceptfile(const char *params, int pid, const Target *t)
{
	Link *l;

	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
	{
		struct transfer_data *td = l->data;
		if (td->topid == pid)
		{
			ft->RequestFile(td->frompid, td->clientpath, td->fname);
			LLRemove(&offers, td);
			LLAdd(&transfers, td);
			goto done;
		}
	}
	chat->SendMessage(pid, "Nobody has offered any files to you.");
done:
	UNLOCK();
}


local void paction(int pid, int action, int arena)
{
	if (action == PA_CONNECT || action == PA_DISCONNECT)
		cancel_files(pid);
}


EXPORT int MM_sendfile(int action, Imodman *mm, int arena)
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

