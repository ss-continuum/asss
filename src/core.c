
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
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


/* STRUCTS */

#include "packets/login.h"

#include "packets/loginresp.h"

typedef struct
{
	AuthData *authdata;
	struct LoginPacket *loginpkt;
	int lplen;
} pdata;


/* PROTOTYPES */

/* packet funcs */
local void PLogin(Player *, byte *, int);
local void MLogin(Player *, const char *);

local void AuthDone(Player *, AuthData *);
local void GSyncDone(Player *);
local void ASyncDone(Player *);

local int SendKeepalive(void *);
local void ProcessLoginQueue(void);
local void SendLoginResponse(Player *);

/* default auth, can be replaced */
local void DefaultAuth(Player *, struct LoginPacket *, int, void (*)(Player *, AuthData *));


/* GLOBALS */

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

local int pdkey;
local u32 contchecksum, codechecksum;

local Iauth _iauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "auth-default", 1)
	DefaultAuth
};


/* FUNCTIONS */


local u32 get_checksum(const char *file)
{
	FILE *f;
	char buf[8192];
	uLong crc = crc32(0, Z_NULL, 0);
	if (!(f = fopen(file, "rb")))
		return (u32)-1;
	while (!feof(f))
	{
		int b = fread(buf, 1, sizeof(buf), f);
		crc = crc32(crc, buf, b);
	}
	fclose(f);
	return crc;
}

local u32 get_u32(const char *file, int offset)
{
	FILE *f;
	u32 buf = -1;
	if ((f = fopen(file, "rb")))
	{
		if (fseek(f, offset, SEEK_SET) == 0)
			fread(&buf, sizeof(u32), 1, f);
		fclose(f);
	}
	return buf;
}


EXPORT int MM_core(int action, Imodman *mm_, Arena *arena)
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
		if (!pd || !lm || !cfg || !map || !aman || !ml) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

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
		ml->SetTimer(SendKeepalive, 500, 500, NULL, NULL);

		contchecksum = get_checksum("clients/continuum.exe");
		codechecksum = get_u32("scrty", 4);

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
		ml->ClearTimer(SendKeepalive, NULL);
		mm->UnregCallback(CB_MAINLOOP, ProcessLoginQueue, ALLARENAS);
		if (net)
		{
			net->RemovePacket(C2S_LOGIN, PLogin);
			net->RemovePacket(C2S_CONTLOGIN, PLogin);
		}
		if (chatnet)
			chatnet->RemoveHandler("LOGIN", MLogin);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(map);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}


void ProcessLoginQueue(void)
{
	int ns, oldstatus;
	Player *player;
	Link *link;

	pd->WriteLock();
	FOR_EACH_PLAYER(player)
	{
		oldstatus = player->status;
		switch (oldstatus)
		{
			/* for all of these states, there's nothing to do in this
			 * loop */
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
				if (player->arena)
					if (player->arena->status == ARENA_RUNNING)
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
				lm->Log(L_ERROR,"<core> [pid=%d] Internal error: unknown player status %d",
						player->pid, oldstatus);
				continue;
		}

		player->status = ns; /* set it */

		/* now unlock status, lock player (because we might be calling
		 * callbacks and we want to have player locked already), and
		 * finally perform the actual action */
		pd->Unlock();
		pd->LockPlayer(player);

		switch (oldstatus)
		{
			case S_NEED_AUTH:
				{
					pdata *d = PPDATA(player, pdkey);
					Iauth *auth = mm->GetInterface(I_AUTH, ALLARENAS);

					if (auth && d->loginpkt != NULL && d->lplen > 0)
					{
						lm->Log(L_DRIVEL, "<core> authenticating with '%s'", auth->head.name);
						auth->Authenticate(player, d->loginpkt, d->lplen, AuthDone);
						mm->ReleaseInterface(auth);
					}
					else
						lm->Log(L_WARN, "<core> Can't authenticate player!");

					afree(d->loginpkt);
					d->loginpkt = NULL;
					d->lplen = 0;
				}
				break;

			case S_NEED_GLOBAL_SYNC:
				if (persist)
					persist->GetPlayer(player, PERSIST_GLOBAL, GSyncDone);
				else
					GSyncDone(player);
				break;

			case S_DO_GLOBAL_CALLBACKS:
				DO_CBS(CB_PLAYERACTION,
				       ALLARENAS,
				       PlayerActionFunc,
					   (player, PA_CONNECT, NULL));
				break;

			case S_SEND_LOGIN_RESPONSE:
				SendLoginResponse(player);
				lm->Log(L_INFO, "<core> [%s] [pid=%d] Player logged in",
						player->name, player->pid);
				break;

			case S_DO_FREQ_AND_ARENA_SYNC:
				/* the arena will be fully loaded here */
				/* first, do pre-callbacks */
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (player, PA_PREENTERARENA, player->arena));
				/* then, get a freq (player->shiptype will be set here
				 * because it's done in PArena) */
				{
					Ifreqman *fm = mm->GetInterface(I_FREQMAN, player->arena);
					int freq = 0, ship = player->p_ship;

					/* if this arena has a manager, use it */
					if (fm)
						fm->InitialFreq(player, &ship, &freq);

					/* set the results back */
					player->p_ship = ship;
					player->p_freq = freq;
				}
				/* then, sync scores */
				if (persist)
					persist->GetPlayer(player, player->arena, ASyncDone);
				else
					ASyncDone(player);
				break;

			case S_SEND_ARENA_RESPONSE:
				/* try to get scores in pdata packet */
				if (stats)
				{
					player->pkt.killpoints = stats->GetStat(player, STAT_KILL_POINTS, INTERVAL_RESET);
					player->pkt.flagpoints = stats->GetStat(player, STAT_FLAG_POINTS, INTERVAL_RESET);
					player->pkt.wins = stats->GetStat(player, STAT_KILLS, INTERVAL_RESET);
					player->pkt.losses = stats->GetStat(player, STAT_DEATHS, INTERVAL_RESET);
				}
				aman->SendArenaResponse(player);
				player->flags.sent_ppk = 0;
				break;

			case S_DO_ARENA_CALLBACKS:
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (player, PA_ENTERARENA, player->arena));
				break;

			case S_LEAVING_ARENA:
				DO_CBS(CB_PLAYERACTION,
				       player->oldarena,
				       PlayerActionFunc,
				       (player, PA_LEAVEARENA, player->oldarena));
				if (persist)
					persist->PutPlayer(player, player->oldarena, NULL);
				break;

			case S_LEAVING_ZONE:
				DO_CBS(CB_PLAYERACTION,
				       ALLARENAS,
				       PlayerActionFunc,
					   (player, PA_DISCONNECT, NULL));
				if (persist)
					persist->PutPlayer(player, PERSIST_GLOBAL, NULL);
				break;
		}

		/* now we release player and take back status */
		pd->UnlockPlayer(player);
		pd->WriteLock();
	}
	pd->Unlock();
}


void PLogin(Player *p, byte *opkt, int l)
{
	pdata *d = PPDATA(p, pdkey);
	int type = p->type;

	if (type != T_VIE && type != T_CONT)
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Login packet from wrong client type (%d)",
				p->pid, type);
#ifdef CFG_RELAX_LENGTH_CHECKS
	else if (l != LEN_LOGINPACKET_VIE && l != LEN_LOGINPACKET_CONT)
#else
	else if ( (type == T_VIE && l != LEN_LOGINPACKET_VIE) ||
	          (type == T_CONT && l != LEN_LOGINPACKET_CONT) )
#endif
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Bad login packet length (%d)", p->pid, l);
	else if (p->status != S_CONNECTED)
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Login request from wrong stage: %d", p->pid, p->status);
	else
	{
		struct LoginPacket *pkt = (struct LoginPacket*)opkt, *lp;
		Player *oldp = pd->FindPlayer(pkt->name);
		int c;

		if (oldp != NULL && oldp != p)
		{
			lm->Log(L_DRIVEL,"<core> [%s] Player already on, kicking him off "
					"(pid %d replacing %d)",
					pkt->name, p->pid, oldp->pid);
			pd->KickPlayer(oldp);
		}

		/* copy into storage for use by authenticator */
		lp = d->loginpkt = amalloc(sizeof(*lp));
		d->lplen = l;
		memcpy(lp, pkt, l);
		p->macid = pkt->macid;
		p->permid = pkt->D2;
		/* replace colons and nonprintables with underscores */
		l = strlen(lp->name);
		for (c = 0; c < l; c++)
			if (lp->name[c] == ':' || lp->name[c] < 32 || lp->name[c] > 126)
				lp->name[c] = '_';
		/* must start with number, letter, or underscore */
		if (!isalnum(lp->name[0]))
			lp->name[0] = '_';
		/* set up status */
		pd->WriteLock();
		p->status = S_NEED_AUTH;
		pd->Unlock();
		lm->Log(L_DRIVEL, "<core> [pid=%d] Login request: '%s'", p->pid, pkt->name);
	}
}


void MLogin(Player *p, const char *line)
{
	pdata *d = PPDATA(p, pdkey);
	const char *t;
	char vers[16];
	struct LoginPacket *lp;
	Player *oldp;
	int c, l;

	if (p->status != S_CONNECTED)
	{
		lm->Log(L_MALICIOUS, "<core> [pid=%d] Login request from wrong stage: %d", p->pid, p->status);
		return;
	}

	lp = d->loginpkt = amalloc(LEN_LOGINPACKET_VIE);
	d->lplen = LEN_LOGINPACKET_VIE;

	/* extract fields */
	t = delimcpy(vers, line, 16, ':');
	if (!t) return;
	lp->cversion = atoi(vers);
	t = delimcpy(lp->name, t, sizeof(lp->name), ':');
	/* replace nonprintables with underscores */
	l = strlen(lp->name);
	for (c = 0; c < l; c++)
		if (lp->name[c] < 32 || lp->name[c] > 126)
			lp->name[c] = '_';
	/* must start with number, letter, or underscore */
	if (!isalnum(lp->name[0]))
		lp->name[0] = '_';
	if (!t) return;
	astrncpy(lp->password, t, sizeof(lp->password));

	oldp = pd->FindPlayer(lp->name);

	if (oldp != NULL && oldp != p)
	{
		lm->Log(L_DRIVEL,"<core> [%s] Player already on, kicking him off "
				"(pid %d replacing %d)",
				lp->name, p->pid, oldp->pid);
		pd->KickPlayer(oldp);
	}

	p->macid = p->permid = 101;
	/* set up status */
	pd->WriteLock();
	p->status = S_NEED_AUTH;
	pd->Unlock();
	lm->Log(L_DRIVEL, "<core> [pid=%d] Login request: '%s'", p->pid, lp->name);
}


void AuthDone(Player *p, AuthData *auth)
{
	pdata *d = PPDATA(p, pdkey);

	if (p->status != S_WAIT_AUTH)
	{
		lm->Log(L_WARN, "<core> [pid=%d] AuthDone called from wrong stage: %d",
				p->pid, p->status);
		return;
	}

	/* copy the authdata */
	d->authdata = amalloc(sizeof(AuthData));
	memcpy(d->authdata, auth, sizeof(AuthData));

	p->flags.authenticated = auth->authenticated;

	if (AUTH_IS_OK(auth->code))
	{
		/* login suceeded */
		/* also copy to player struct */
		strncpy(p->pkt.name, auth->sendname, 20);
		astrncpy(p->name, auth->name, 21);
		strncpy(p->pkt.squad, auth->squad, 20);
		astrncpy(p->squad, auth->squad, 21);

		/* increment stage */
		pd->WriteLock();
		p->status = S_NEED_GLOBAL_SYNC;
		pd->Unlock();
	}
	else
	{
		/* if the login didn't succeed status should go to S_CONNECTED
		 * instead of moving forward, and send the login response now,
		 * since we won't do it later. */
		SendLoginResponse(p);
		pd->WriteLock();
		p->status = S_CONNECTED;
		pd->Unlock();
	}
}


void GSyncDone(Player *p)
{
	pd->WriteLock();
	if (p->status != S_WAIT_GLOBAL_SYNC)
		lm->Log(L_WARN, "<core> [pid=%s] GSyncDone called from wrong stage", p->pid, p->status);
	else
		p->status = S_DO_GLOBAL_CALLBACKS;
	pd->Unlock();
}


void ASyncDone(Player *p)
{
	pd->WriteLock();
	if (p->status != S_WAIT_ARENA_SYNC)
		lm->Log(L_WARN, "<core> [pid=%s] ASyncDone called from wrong stage", p->pid, p->status);
	else
		p->status = S_SEND_ARENA_RESPONSE;
	pd->Unlock();
}


local const char *get_auth_code_msg(int code)
{
	switch (code)
	{
		case AUTH_OK: return "ok";
		case AUTH_NEWNAME: return "new user";
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
		case AUTH_TOOLOWUSAGE: return "too low usage";
		case AUTH_NONAME: return "no name sent";
		case AUTH_TOOMANYDEMO: return "too many demo players";
		case AUTH_NODEMO: return "no demo players allowed";
		default: return "???";
	}
}


void SendLoginResponse(Player *p)
{
	pdata *d = PPDATA(p, pdkey);
	AuthData *auth = d->authdata;

	if (!auth)
	{
		lm->Log(L_ERROR, "<core> missing authdata for pid %d", p->pid);
		pd->KickPlayer(p);
	}
	else if (IS_STANDARD(p))
	{
		struct S2CLoginResponse lr =
			{ S2C_LOGINRESPONSE, 0, 134, 0, {0, 0, 0},
				0, {0, 0, 0, 0, 0}, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
		lr.code = auth->code;
		lr.demodata = auth->demodata;
		lr.newschecksum = map->GetNewsChecksum();

		if (p->type == T_CONT)
		{
			struct {
				u8 type;
				u16 contversion;
				u32 checksum;
			} pkt = { S2C_CONTVERSION, 38, contchecksum };
			net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);

			lr.exechecksum = contchecksum;
			lr.codechecksum = codechecksum;
		}
		else
		{
			/* old vie exe checksums */
			lr.exechecksum = 0xF1429CE8;
			lr.codechecksum = 0x281CC948;
		}

		if (capman && capman->HasCapability(p, "seeprivfreq"))
		{
			/* to make the client think it's a mod, set these checksums to -1 */
			lr.exechecksum = -1;
			lr.codechecksum = -1;
		}

		if (lr.code == AUTH_CUSTOMTEXT)
		{
			if (p->type == T_CONT)
			{
				/* send custom rejection text */
				byte custom[256];
				custom[0] = S2C_LOGINTEXT;
				astrncpy(custom+1, auth->customtext, 255);
				net->SendToOne(p, custom, strlen(custom+1) + 2, NET_RELIABLE);
			}
			else /* vie doesn't understand that packet */
				lr.code = AUTH_LOCKEDOUT;
		}

		net->SendToOne(p, (char*)&lr, sizeof(lr), NET_RELIABLE);
	}
	else if (IS_CHAT(p))
	{
		if (AUTH_IS_OK(auth->code))
			chatnet->SendToOne(p, "LOGINOK:%s", p->name);
		else
			chatnet->SendToOne(p, "LOGINBAD:%s", get_auth_code_msg(auth->code));
	}

	afree(auth);
	d->authdata = NULL;
}


void DefaultAuth(Player *p, struct LoginPacket *pkt, int lplen,
		void (*Done)(Player *p, AuthData *auth))
{
	AuthData auth;

	memset(&auth, 0, sizeof(auth));
	auth.demodata = 0;
	auth.code = AUTH_OK;
	auth.authenticated = FALSE;
	astrncpy(auth.name, pkt->name, 24);
	strncpy(auth.sendname, pkt->name, 20);
	memset(auth.squad, 0, sizeof(auth.squad));

	Done(p, &auth);
}


int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	if (net)
		net->SendToArena(ALLARENAS, NULL, &keepalive, 1, NET_RELIABLE);
	return 1;
}


