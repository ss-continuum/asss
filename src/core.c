
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
local void MLogin(int, const char *);

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

local Imodman *mm;
local Iplayerdata *pd;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Ilogman *lm;
local Imapnewsdl *map;
local Iarenaman *aman;
local Icapman *capman;
local Ipersist *persist;
local Istats *stats;

local PlayerData *players;

local Iauth _iauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "auth-default", 1)
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
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		map = mm->GetInterface(I_MAPNEWSDL, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		players = pd->players;

		if (!pd || !lm || !cfg || !map || !aman || !ml) return MM_FAIL;

		/* set up callbacks */
		if (net)
		{
			net->AddPacket(C2S_LOGIN, PLogin);
			net->AddPacket(C2S_CONTLOGIN, PLogin);
		}
		if (chatnet)
			chatnet->AddHandler("LOGIN", MLogin);

		mm->RegCallback(CB_MAINLOOP, ProcessLoginQueue, ALLARENAS);

		/* register default interfaces which may be replaced later */
		mm->RegInterface(&_iauth, ALLARENAS);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);

		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(stats);
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_iauth, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_MAINLOOP, ProcessLoginQueue, ALLARENAS);
		if (net)
		{
			net->RemovePacket(C2S_LOGIN, PLogin);
			net->RemovePacket(C2S_CONTLOGIN, PLogin);
		}
		if (chatnet)
			chatnet->RemoveHandler("LOGIN", MLogin);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(map);
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
					int len = LEN_LOGINPACKET_VIE;

					/* figuring out the length this way is guaranteed to
					 * work because of the length check in PLogin. */
#ifndef CFG_RELAX_LENGTH_CHECKS
					if (player->type == T_CONT)
						len = LEN_LOGINPACKET_CONT;
#endif

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
				else
					GSyncDone(pid);
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
				else
					ASyncDone(pid);
				break;

			case S_SEND_ARENA_RESPONSE:
				/* try to get scores in pdata packet */
				if (stats)
				{
					player->killpoints = stats->GetStat(pid, STAT_KILL_POINTS, INTERVAL_RESET);
					player->flagpoints = stats->GetStat(pid, STAT_FLAG_POINTS, INTERVAL_RESET);
					player->wins = stats->GetStat(pid, STAT_KILLS, INTERVAL_RESET);
					player->losses = stats->GetStat(pid, STAT_DEATHS, INTERVAL_RESET);
				}
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
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Login packet from wrong client type (%d)",
				pid, type);
#ifdef CFG_RELAX_LENGTH_CHECKS
	else if (l != LEN_LOGINPACKET_VIE && l != LEN_LOGINPACKET_CONT)
#else
	else if ( (type == T_VIE && l != LEN_LOGINPACKET_VIE) ||
	          (type == T_CONT && l != LEN_LOGINPACKET_CONT) )
#endif
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Bad login packet length (%d)", pid, l);
	else if (players[pid].status != S_CONNECTED)
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Login request from wrong stage: %d", pid, players[pid].status);
	else
	{
		struct LoginPacket *pkt = (struct LoginPacket*)p;
		int oldpid = pd->FindPlayer(pkt->name), c;

		if (oldpid != -1 && oldpid != pid)
		{
			lm->Log(L_DRIVEL,"<core> [%s] Player already on, kicking him off "
					"(pid %d replacing %d)",
					pkt->name, pid, oldpid);
			pd->KickPlayer(oldpid);
		}

		/* copy into storage for use by authenticator */
		memcpy(bigloginpkt + pid, p, l);
		/* replace colons with underscores */
		for (c = 0; c < sizeof(bigloginpkt->name); c++)
			if (bigloginpkt[pid].name[c] == ':')
				bigloginpkt[pid].name[c] = '_';
		/* set up status */
		players[pid].status = S_NEED_AUTH;
		lm->Log(L_DRIVEL, "<core> [pid=%d] Login request: '%s'", pid, pkt->name);
	}
}


void MLogin(int pid, const char *line)
{
	const char *t;
	char vers[16];
	struct LoginPacket pkt;
	int oldpid;

	memset(&pkt, 0, sizeof(pkt));

	/* extract fields */
	t = delimcpy(vers, line, 16, ':');
	if (!t) return;
	pkt.cversion = atoi(vers);
	t = delimcpy(pkt.name, t, sizeof(pkt.name), ':');
	if (!t) return;
	astrncpy(pkt.password, t, sizeof(pkt.password));

	oldpid = pd->FindPlayer(pkt.name);

	if (oldpid != -1 && oldpid != pid)
	{
		lm->Log(L_DRIVEL,"<core> [%s] Player already on, kicking him off "
				"(pid %d replacing %d)",
				pkt.name, pid, oldpid);
		pd->KickPlayer(oldpid);
	}

	/* copy into storage for use by authenticator */
	memcpy(bigloginpkt + pid, &pkt, sizeof(pkt));
	/* set up status */
	players[pid].status = S_NEED_AUTH;
	lm->Log(L_DRIVEL, "<core> [pid=%d] Login request: '%s'", pid, pkt.name);
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

	if (AUTH_IS_OK(auth->code))
	{
		/* login suceeded */
		/* also copy to player struct */
		strncpy(player->sendname, auth->sendname, 20);
		astrncpy(player->name, auth->name, 21);
		strncpy(player->sendsquad, auth->squad, 20);
		astrncpy(player->squad, auth->squad, 21);

		/* increment stage */
		pd->LockStatus();
		player->status = S_NEED_GLOBAL_SYNC;
		pd->UnlockStatus();
	}
	else
	{
		/* stuff other than AUTH_OK means the login didn't succeed.
		 * status should go to S_CONNECTED instead of moving forward,
		 * and send the login response now, since we won't do it later. */
		SendLoginResponse(pid);
		pd->LockStatus();
		player->status = S_CONNECTED;
		pd->UnlockStatus();
	}
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


local const char *get_auth_code_msg(int code)
{
	switch (code)
	{
		case AUTH_OK: return "ok";
		case AUTH_UNKNOWN: return "unknown name";
		case AUTH_BADPASSWORD: return "incorrect password";
		case AUTH_ARENAFULL: return "arena full";
		case AUTH_LOCKEDOUT: return "you have been locked out";
		case AUTH_NOPERMISSION: return "no permission";
		case AUTH_SPECONLY: return "you can spec only";
		case AUTH_TOOMANYPOINTS: return "you have too many points";
		case AUTH_TOOSLOW: return "too slow (?)";
		case AUTH_NOPERMISSION2: return "no permission (2)";
		case AUTH_NONEWCONN: return "the server is not accepting new connections";
		case AUTH_BADNAME: return "bad player name";
		case AUTH_OFFENSIVENAME: return "offensive player name";
		case AUTH_NOSCORES: return "the server is not recordng scores";
		case AUTH_SERVERBUSY: return "the server is busy";
		case AUTH_EXPONLY: return "???";
		case AUTH_ISDEMO: return "???";
		case AUTH_TOOMANYDEMO: return "too many demo players";
		case AUTH_NODEMO: return "no demo players allowed";
		default: return "???";
	}
}


void SendLoginResponse(int pid)
{
	AuthData *auth = bigauthdata + pid;

	if (IS_STANDARD(pid))
	{
		struct LoginResponse lr =
			{ S2C_LOGINRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
				0, 0x281CC948, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
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
	else if (IS_CHAT(pid))
	{
		if (AUTH_IS_OK(auth->code))
			chatnet->SendToOne(pid, "LOGINOK:%s", players[pid].name);
		else
			chatnet->SendToOne(pid, "LOGINBAD:%s", get_auth_code_msg(auth->code));
	}
}


void DefaultAuth(int pid, struct LoginPacket *p, int lplen,
		void (*Done)(int, AuthData *))
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	strncpy(auth.sendname, p->name, 20);
	memset(auth.squad, 0, sizeof(auth.squad));

	Done(pid, &auth);
}


int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	if (net)
		net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


