
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "zlib.h"

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

local int SendKeepalive(void *);
local void ProcessLoginQueue(void);
local void SendLoginResponse(int);

/* default auth, can be replaced */
local void DefaultAuth(int, struct LoginPacket *, int, void (*)(int, AuthData *));


/* GLOBALS */

AuthData bigauthdata[MAXPLAYERS];
struct LoginPacket bigloginpkt[MAXPLAYERS];

local Iplayerdata *pd;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *lm;
local Imapnewsdl *map;
local Iarenaman *aman;
local Ipersist *persist;
local Icapman *capman;

local PlayerData *players;

local Iauth _iauth =
{
	INTERFACE_HEAD_INIT("auth-default")
	DefaultAuth
};


/* FUNCTIONS */

EXPORT int MM_core(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		map = mm->GetInterface(I_MAPNEWSDL, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		players = pd->players;

		if (!pd || !lm || !cfg || !map || !aman || !net || !ml) return MM_FAIL;

		/* set up callbacks */
		net->AddPacket(C2S_LOGIN, PLogin);
		net->AddPacket(C2S_CONTLOGIN, PLogin);
		mm->RegCallback(CB_MAINLOOP, ProcessLoginQueue, ALLARENAS);

		/* register default interfaces which may be replaced later */
		mm->RegInterface(I_AUTH, &_iauth, ALLARENAS);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(I_AUTH, &_iauth, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_MAINLOOP, ProcessLoginQueue, ALLARENAS);
		net->RemovePacket(C2S_LOGIN, PLogin);
		net->RemovePacket(C2S_CONTLOGIN, PLogin);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(map);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}


void ProcessLoginQueue(void)
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
			case S_SEND_ARENA_RESPONSE: ns = S_DO_ARENA_CALLBACKS;  break;
			case S_DO_ARENA_CALLBACKS:  ns = S_PLAYING;             break;
			case S_LEAVING_ARENA:       ns = S_LOGGEDIN;            break;
			case S_LEAVING_ZONE:        ns = S_TIMEWAIT;            break;

			case S_LOGGEDIN:
				/* check if the player's arena is ready.
				 * LOCK: we don't grab the arena status lock because it
				 * doesn't matter if we miss it this time around */
				if (ARENA_OK(player->arena))
					if (aman->arenas[player->arena].status == ARENA_RUNNING)
						player->status = S_DO_FREQ_AND_ARENA_SYNC;

				/* check whenloggedin. this is used to move players to
				 * the leaving_zone status once various things are
				 * completed */
				if (player->whenloggedin)
				{
					player->status = player->whenloggedin;
					player->whenloggedin = 0;
				}

				continue;

			default:
				lm->Log(L_ERROR,"<core> [pid=%d] Internal error: unknown player status %d", pid, oldstatus);
				break;
		}

		/* check for missing persist module */
		if (!persist && (ns == S_WAIT_GLOBAL_SYNC || ns == S_WAIT_ARENA_SYNC) )
			ns++;

		player->status = ns; /* set it */

		/* now unlock status, lock player (because we might be calling
		 * callbacks and we want to have player locked already), and
		 * finally perform the actual action */
		pd->UnlockStatus();
		pd->LockPlayer(pid);

		switch (oldstatus)
		{
			case S_NEED_AUTH:
				{
					Iauth *auth = mm->GetInterface(I_AUTH, ALLARENAS);
					int len = LEN_LOGINPACKET_CONT;

					/* figuring out the length this way is guaranteed to
					 * work because of the length check in PLogin */
					if (player->type == T_VIE)
						len = LEN_LOGINPACKET_VIE;

					if (auth)
					{
						auth->Authenticate(pid, bigloginpkt + pid, len, AuthDone);
						mm->ReleaseInterface(auth);
					}
					else
						lm->Log(L_ERROR, "<core> Missing auth module!");
				}
				break;

			case S_NEED_GLOBAL_SYNC:
				if (persist)
					persist->SyncFromFile(pid, PERSIST_GLOBAL, GSyncDone);
				break;

			case S_DO_GLOBAL_CALLBACKS:
				DO_CBS(CB_PLAYERACTION,
				       ALLARENAS,
				       PlayerActionFunc,
					   (pid, PA_CONNECT, -1));
				break;

			case S_SEND_LOGIN_RESPONSE:
				SendLoginResponse(pid);
				lm->Log(L_INFO, "<core> [%s] [pid=%d] Player logged in",
						player->name, pid);
				break;

			case S_DO_FREQ_AND_ARENA_SYNC:
				/* first, do pre-callbacks */
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (pid, PA_PREENTERARENA, player->arena));
				/* then, get a freq */
				/* yes, player->shiptype will be set here because it's
				 * done in PArena */
				{
					Ifreqman *fm = mm->GetInterface(I_FREQMAN, player->arena);
					int freq = 0, ship = player->shiptype;

					/* if this arena has a manager, use it */
					if (fm)
						fm->InitialFreq(pid, &ship, &freq);

					/* set the results back */
					player->shiptype = ship;
					player->freq = freq;
				}
				/* then, sync scores */
				if (persist)
					persist->SyncFromFile(pid, player->arena, ASyncDone);
				break;

			case S_SEND_ARENA_RESPONSE:
				aman->SendArenaResponse(pid);
				break;

			case S_DO_ARENA_CALLBACKS:
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (pid, PA_ENTERARENA, player->arena));
				break;

			case S_LEAVING_ARENA:
				DO_CBS(CB_PLAYERACTION,
				       player->oldarena,
				       PlayerActionFunc,
				       (pid, PA_LEAVEARENA, player->oldarena));
				if (persist)
					persist->SyncToFile(pid, player->oldarena, NULL);
				break;

			case S_LEAVING_ZONE:
				DO_CBS(CB_PLAYERACTION,
				       ALLARENAS,
				       PlayerActionFunc,
					   (pid, PA_DISCONNECT, -1));
				if (persist)
					persist->SyncToFile(pid, PERSIST_GLOBAL, NULL);
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
	int type = players[pid].type;

	if (type != T_VIE && type != T_CONT)
		lm->Log(L_MALICIOUS,"<core> [pid=%d] Login packet from wrong client type (%d)",
				pid, type);
	else if ( (type == T_VIE && l != LEN_LOGINPACKET_VIE) ||
	          (type == T_CONT && l != LEN_LOGINPACKET_CONT) )
		lm->Log(L_MALICIOUS,"<core> [pid=%d] Bad login packet length (%d)", pid, l);
	else if (players[pid].status != S_CONNECTED)
		lm->Log(L_MALICIOUS,"<core> [pid=%d] Login request from wrong stage: %d", pid, players[pid].status);
	else
	{
		struct LoginPacket *pkt = (struct LoginPacket*)p;
		int oldpid = pd->FindPlayer(pkt->name);

		if (oldpid != -1 && oldpid != pid)
		{
			lm->Log(L_DRIVEL,"<core> [%s] Player already on, kicking him off "
					"(pid %d replacing %d)",
					pkt->name, pid, oldpid);
			net->DropClient(oldpid);
		}

		memcpy(bigloginpkt + pid, p, l);
		players[pid].status = S_NEED_AUTH;
		lm->Log(L_DRIVEL,"<core> [pid=%d] Login request: \"%s\"", pid, pkt->name);
	}
}


void AuthDone(int pid, AuthData *auth)
{
	PlayerData *player = players + pid;

	if (player->status != S_WAIT_AUTH)
	{
		lm->Log(L_WARN, "<core> [pid=%d] AuthDone called from wrong stage: %d", pid, player->status);
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
		lm->Log(L_WARN, "<core> [pid=%s] GSyncDone called from wrong stage", pid, players[pid].status);
	else
		players[pid].status = S_DO_GLOBAL_CALLBACKS;
	pd->UnlockStatus();
}


void ASyncDone(int pid)
{
	pd->LockStatus();
	if (players[pid].status != S_WAIT_ARENA_SYNC)
		lm->Log(L_WARN, "<core> [pid=%s] ASyncDone called from wrong stage", pid, players[pid].status);
	else
		players[pid].status = S_SEND_ARENA_RESPONSE;
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

	if (capman && capman->HasCapability(pid, "seeprivfreq"))
	{
		/* to make the client think it's a mod, set these checksums to -1 */
		lr.exechecksum = -1;
		lr.blah3 = -1;
	}

	net->SendToOne(pid, (char*)&lr, sizeof(lr), NET_RELIABLE);
}


void DefaultAuth(int pid, struct LoginPacket *p, int lplen,
		void (*Done)(int, AuthData *))
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	memset(auth.squad, 0, sizeof(auth.squad));

	Done(pid, &auth);
}


int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


