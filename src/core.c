
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


/* STRUCTS */

#include "packets/login.h"

#include "packets/loginresp.h"


/* PROTOTYPES */

/* packet funcs */
local void PLogin(int, byte *, int);

local void AuthDone(int, AuthData *);
local void GSyncDone(int);
local void ASyncDone(int);

local void CallPA(int pid, int action);
local int SendKeepalive(void *);
local void ProcessLoginQueue();
local void SendLoginResponse(int);

/* default auth, can be replaced */
local void DefaultAuth(int, struct LoginPacket *, void (*)(int, AuthData *));
local int DefaultAssignFreq(int, int, byte);


/* GLOBALS */

AuthData bigauthdata[MAXPLAYERS];
struct LoginPacket bigloginpkt[MAXPLAYERS];

local Iplayerdata *pd;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Imapnewsdl *map;
local Iauth *auth;
local Iassignfreq *afreq;
local Iarenaman *aman;

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
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_MAPNEWSDL, &map);
		mm->RegInterest(I_AUTH, &auth);
		mm->RegInterest(I_ASSIGNFREQ, &afreq);
		mm->RegInterest(I_ARENAMAN, &aman);
		players = pd->players;

		if (!net || !ml) return MM_FAIL;

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
		mm->UnregInterface(I_ASSIGNFREQ, &_iaf);
		mm->UnregInterface(I_AUTH, &_iauth);
		net->RemovePacket(C2S_LOGIN, PLogin);
		mm->UnregInterest(I_PLAYERDATA, &pd);
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
	int pid, ns, oldstatus;
	PlayerData *player;

	pd->LockStatus();
	for (pid = 0, player = players; pid < MAXPLAYERS; pid++, player++)
	{
		oldstatus = player->status;
		switch (oldstatus)
		{
			/* for all of these states, there's nothing to do in this
			 * loop */
			case S_FREE:
			case S_CONNECTED:
			case S_WAIT_AUTH:
			case S_WAIT_GLOBAL_SYNC:
			/* case S_LOGGEDIN: */
			case S_WAIT_ARENA_SYNC:
			case S_PLAYING:
			case S_TIMEWAIT:
			case S_TIMEWAIT2:
				continue;
		}

		/* while still holding the status lock, set the new status.
		 * we do this now so that if any of the actions taken by the
		 * status responses modify the status, the change isn't lost.
		 * yeah, it's all because of DefaultAuth. bah. */
		switch (oldstatus)
		{
			case S_NEED_AUTH:           ns = S_WAIT_AUTH;           break;
			case S_NEED_GLOBAL_SYNC:    ns = S_WAIT_GLOBAL_SYNC;    break;
			case S_DO_GLOBAL_CALLBACKS: ns = S_SEND_LOGIN_RESPONSE; break;
			case S_SEND_LOGIN_RESPONSE: ns = S_LOGGEDIN;            break;
			case S_DO_FREQ_AND_ARENA_SYNC: ns = S_WAIT_ARENA_SYNC;  break;
			case S_DO_ARENA_CALLBACKS:  ns = S_SEND_ARENA_RESPONSE; break;
			case S_SEND_ARENA_RESPONSE: ns = S_PLAYING;             break;
			case S_LEAVING_ARENA:       ns = S_LOGGEDIN;            break;
			case S_LEAVING_ZONE:        ns = S_TIMEWAIT;            break;

			case S_LOGGEDIN:
				/* check whenloggedin. this is used to move players to
				 * the leaving_zone status once various things are
				 * completed */
				if (player->whenloggedin)
					player->status = player->whenloggedin;

				/* check if the player's arena is ready.
				 * LOCK: we don't grab the arena status lock because it
				 * doesn't matter if we miss it this time around */
				if (aman->data[player->arena].status == ARENA_RUNNING)
					player->status = S_DO_FREQ_AND_ARENA_SYNC;

				continue;

			default:
				log->Log(LOG_ERROR,"Internal error: unknown player status!");
				break;
		}
		player->status = ns; /* set it */

		/* now unlock status, lock player (because we might be calling
		 * callbacks and we want to have player locked already), and
		 * finally perform the actual action */
		pd->UnlockStatus();
		pd->LockPlayer(pid);

		/*log->Log(LOG_DEBUG,"Processing status %i for pid %i",oldstatus,pid);*/

		switch (oldstatus)
		{
			case S_NEED_AUTH:
				auth->Authenticate(pid, bigloginpkt + pid, AuthDone);
				break;

			case S_NEED_GLOBAL_SYNC:
				/* FIXME: scoreman->SyncFromFileAsync(pid, 1, GSyncDone); */
				GSyncDone(pid);
				break;

			case S_DO_GLOBAL_CALLBACKS:
				CallPA(pid, PA_CONNECT);
				break;

			case S_SEND_LOGIN_RESPONSE:
				SendLoginResponse(pid);
				log->Log(LOG_INFO, "Player logged on (%s)", player->name);
				break;

			case S_DO_FREQ_AND_ARENA_SYNC:
				/* first get a freq */
				/* yes, player->shiptype will be set here because it's
				 * done in PArena */
				player->freq = afreq->AssignFreq(pid, BADFREQ,
						player->shiptype);
				/* then, sync scores */
				/* FIXME: scoreman->SyncFromFileAsync(pid, 0, ASyncDone); */
				ASyncDone(pid);
				break;

			case S_DO_ARENA_CALLBACKS:
				CallPA(pid, PA_ENTERARENA);
				break;

			case S_SEND_ARENA_RESPONSE:
				aman->SendArenaResponse(pid);
				break;

			case S_LEAVING_ARENA:
				CallPA(pid, PA_LEAVEARENA);
				/* FIXME: scoreman->SyncToFile(pid, 0); */
				break;

			case S_LEAVING_ZONE:
				CallPA(pid, PA_DISCONNECT);
				/* FIXME: scoreman->SyncToFile(pid, 1); */
				break;
		}

		/* now we release player and take back status */
		pd->UnlockPlayer(pid);
		pd->LockStatus();
	}
	pd->UnlockStatus();
}



void PLogin(int pid, byte *p, int l)
{
	if (l != sizeof(struct LoginPacket))
		log->Log(LOG_BADDATA,"Bad login packet length (%i)",pid);
	else if (players[pid].status != S_CONNECTED)
		log->Log(LOG_BADDATA,"Login request in wrong stage: %i (%i)", players[pid].status, pid);
	else
	{
		memcpy(bigloginpkt + pid, p, sizeof(struct LoginPacket));
		players[pid].status = S_NEED_AUTH;
	}
}


void CallPA(int pid, int action)
{
	LinkedList *lst;
	Link *l;

	lst = mm->LookupCallback(CALLBACK_PLAYERACTION);
	for (l = LLGetHead(lst); l; l = l->next)
		((PlayerActionFunc)l->data)(pid, action);
	LLFree(lst);
}


void AuthDone(int pid, AuthData *auth)
{
	PlayerData *player = players + pid;

	if (player->status != S_WAIT_AUTH)
	{
		log->Log(LOG_BADDATA, "AuthDone called from wrong stage: %i (%i)", player->status, pid);
		return;
	}

	/* copy the authdata */
	memcpy(bigauthdata + pid, auth, sizeof(AuthData));

	/* also copy to player struct */
	strncpy(player->sendname, auth->name, 20);
	astrncpy(player->name, auth->name, 21);
	strncpy(player->sendsquad, auth->squad, 20);
	astrncpy(player->squad, auth->squad, 21);

	/* increment stage */
	pd->LockStatus();
	player->status = S_NEED_GLOBAL_SYNC;
	pd->UnlockStatus();
}


void GSyncDone(int pid)
{
	pd->LockStatus();
	if (players[pid].status != S_WAIT_GLOBAL_SYNC)
		log->Log(LOG_BADDATA, "GSyncDone called from wrong stage: %i (%i)", players[pid].status, pid);
	else
		players[pid].status = S_DO_GLOBAL_CALLBACKS;
	pd->UnlockStatus();
}


void ASyncDone(int pid)
{
	pd->LockStatus();
	if (players[pid].status != S_WAIT_ARENA_SYNC)
		log->Log(LOG_BADDATA, "ASyncDone called from wrong stage: %i (%i)", players[pid].status, pid);
	else
		players[pid].status = S_DO_ARENA_CALLBACKS;
	pd->UnlockStatus();
}


void SendLoginResponse(int pid)
{
	struct LoginResponse lr =
		{ S2C_LOGINRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
			0, 0x281CC948, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
	AuthData *auth;

	auth = bigauthdata + pid;

	lr.code = auth->code;
	lr.demodata = auth->demodata;
	lr.newschecksum = map->GetNewsChecksum();
	net->SendToOne(pid, (char*)&lr, sizeof(lr), NET_RELIABLE);
}


void DefaultAuth(int pid, struct LoginPacket *p, void (*Done)(int, AuthData *))
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	memset(auth.squad, 0, sizeof(auth.squad));

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


