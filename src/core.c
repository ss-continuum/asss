
#include <zlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "asss.h"



#define EXECHECKSUM 0xF1429CE8

#define STAGE_INIT          1
#define STAGE_WAITFORAUTH   2
#define STAGE_NEEDSCORES    3
#define STAGE_WAITFORSCORES 4
#define STAGE_DOCALLBACKS   5
#define STAGE_DONE          6


/* STRUCTS */

#include "packets/login.h"

#include "packets/loginresp.h"

typedef struct LoginQueueData
{
	int pid, stage;
	struct LoginPacket pkt;
	AuthData auth;
} LoginQueueData;


/* PROTOTYPES */

/* packet funcs */
local void PLogin(int, byte *, int);

local void AuthDone(int, AuthData *);
local void ScoresDone(int);

local int SendKeepalive(void *);
local void ProcessLoginQueue();
local void SendLoginResponse(LoginQueueData *);

/* default auth, can be replaced */
local void DefaultAuth(int, struct LoginPacket *, void (*)(int, AuthData *));
local int DefaultAssignFreq(int, int, byte);


/* GLOBALS */

LinkedList loginqueue;
Mutex lqmtx;

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Imapnewsdl *map;
local Iauth *auth;

local PlayerData *players;

local Iassignfreq _iaf = { DefaultAssignFreq };
local Iauth _iauth = { DefaultAuth };


/* FUNCTIONS */

int MM_core(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_MAPNEWSDL, &map);
		mm->RegInterest(I_AUTH, &auth);
		players = mm->players;

		if (!net || !ml) return MM_FAIL;

		/* init stuff */
		LLInit(&loginqueue);
		InitMutex(&lqmtx);

		/* set up callbacks */
		net->AddPacket(C2S_LOGIN, PLogin);
		mm->RegCallback(CALLBACK_MAINLOOP, ProcessLoginQueue);

		/* register default interfaces which may be replaced later */
		mm->RegInterface(I_AUTH, &_iauth);
		mm->RegInterface(I_ASSIGNFREQ, &_iaf);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);
	}
	else if (action == MM_UNLOAD)
	{
		LLEmpty(&loginqueue);
		mm->UnregInterface(I_ASSIGNFREQ, &_iaf);
		mm->UnregInterface(I_AUTH, &_iauth);
		net->RemovePacket(C2S_LOGIN, PLogin);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_MAINLOOP, &ml);
		mm->UnregInterest(I_MAPNEWSDL, &map);
		mm->UnregInterest(I_AUTH, &auth);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "core - handles core game packets, including logins";
	}
	return MM_OK;
}


void ProcessLoginQueue()
{
	Link *l;
	LoginQueueData *lq;

	LockMutex(&lqmtx);
	for (l = LLGetHead(&loginqueue); l; l = l->next)
	{
		
		lq = l->data;
		switch (lq->stage)
		{
			case STAGE_INIT:
				lq->stage++;
				auth->Authenticate(lq->pid, &lq->pkt, AuthDone);
				break;

			case STAGE_WAITFORAUTH:
				break;

			case STAGE_NEEDSCORES:
				lq->stage++;
				//scoreman->SyncFromFileAsync(lq->pid, 1, ScoresDone);
				break;

			case STAGE_WAITFORSCORES:
				break;

			case STAGE_DOCALLBACKS:
				{
					LinkedList *lst;
					Link *l;

					lst = mm->LookupCallback(CALLBACK_PLAYERACTION);
					UnlockMutex(&lqmtx); /* don't hold mutex here, this might take a while */
					for (l = LLGetHead(lst); l; l = l->next)
						((PlayerActionFunc)l->data)(lq->pid, PA_CONNECT);
					LockMutex(&lqmtx); /* get mutex back again */
				}
				lq->stage++;
				break;
		}

		if (lq->stage >= STAGE_DONE)
		{
			SendLoginResponse(lq);
			LLRemove(&loginqueue, lq);
			log->Log(LOG_INFO, "Player logged on (%s)", players[lq->pid].name);
			afree(lq);
		}
	}
	UnlockMutex(&lqmtx);
}


void PLogin(int pid, byte *p, int l)
{
	if (l != sizeof(struct LoginPacket))
		log->Log(LOG_BADDATA,"Bad login packet length (%i)",pid);
	else
	{
		LoginQueueData *lq;

		lq = amalloc(sizeof(LoginQueueData));
		lq->pid = pid;
		lq->stage = STAGE_INIT;
		memcpy(&lq->pkt, p, sizeof(struct LoginPacket));

		LockMutex(&lqmtx);
		LLAdd(&loginqueue, lq);
		UnlockMutex(&lqmtx);
	}
}


void AuthDone(int pid, AuthData *auth)
{
	Link *l;
	LoginQueueData *lq;

	LockMutex(&lqmtx);

	for (l = LLGetHead(&loginqueue); l; l = l->next)
	{
		lq = l->data;
		if (lq->pid == pid)
			break;
	}

	if (lq->pid == pid && lq->stage != STAGE_WAITFORAUTH)
	{
		log->Log(LOG_BADDATA, "AuthDone called when the loginqueue entry is not at STAGE_WAITFORAUTH");
		LLRemove(&loginqueue, lq);
		afree(lq);
		UnlockMutex(&lqmtx);
		return;
	}

	UnlockMutex(&lqmtx);

	if (lq->pid != pid)
	{
		log->Log(LOG_BADDATA, "AuthDone called on pid that's not in the loginqueue: %i", pid);
		return;
	}

	/* copy the authdata */
	memcpy(&lq->auth, auth, sizeof(AuthData));

	/* also copy to player struct */
	strncpy(players[pid].sendname, auth->name, 20);
	astrncpy(players[pid].name, auth->name, 21);
	strncpy(players[pid].sendsquad, auth->squad, 20);
	astrncpy(players[pid].squad, auth->squad, 21);

	lq->stage++;
}


void ScoresDone(int pid)
{
	Link *l;
	LoginQueueData *lq;

	LockMutex(&lqmtx);

	for (l = LLGetHead(&loginqueue); l; l = l->next)
	{
		lq = l->data;
		if (lq->pid == pid)
			break;
	}

	if (lq->pid == pid && lq->stage != STAGE_WAITFORSCORES)
	{
		log->Log(LOG_BADDATA, "ScoresDone called when the loginqueue entry is not at STAGE_WAITFORSCORES");
		LLRemove(&loginqueue, lq);
		afree(lq);
		UnlockMutex(&lqmtx);
		return;
	}

	UnlockMutex(&lqmtx);

	if (lq->pid != pid)
	{
		log->Log(LOG_BADDATA, "ScoresDone called on pid that's not in the loginqueue: %i", pid);
		return;
	}

	/* increment stage */
	lq->stage++;
}


void SendLoginResponse(LoginQueueData *lq)
{
	struct LoginResponse lr =
		{ S2C_LOGINRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
			0, 0x281CC948, 0, {0, 0, 0, 0, 0, 0, 0, 0} };

	lr.code = lq->auth.code;
	lr.demodata = lq->auth.demodata;
	lr.newschecksum = map->GetNewsChecksum();
	net->SendToOne(lq->pid, (char*)&lr, sizeof(lr), NET_RELIABLE);
}


void DefaultAuth(int pid, struct LoginPacket *p, void (*Done)(int, AuthData *))
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	auth.squad[0] = 0;

	Done(pid, &auth);
}


int DefaultAssignFreq(int pid, int freq, byte ship)
{
	return freq;
}


int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


