
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef WIN32
#include <arpa/inet.h>
#else
#include <malloc.h>
#include <winsock.h>
#endif

#include "asss.h"

#include "packets/login.h"

#include "packets/billmisc.h"

/* prototypes */

/* interface: */
local void SendToBiller(byte *, int, int);
local void AddPacket(byte, PacketFunc);
local void RemovePacket(byte, PacketFunc);
local int GetStatus(void);

/* local: */
local void BillingAuth(int, struct LoginPacket *, int, void (*)(int, AuthData*));

local int SendPing(void *);
local void SendLogin(int, byte *, int);

local void BAuthResponse(int, byte *, int);
local void BChatMsg(int, byte *, int);
local void BMessage(int, byte *, int);
local void BSingleMessage(int, byte *, int);

local void PChat(int, byte *, int);
local void MChat(int, const char *);

local void DefaultCmd(const char *, int, const Target *);

local void Cusage(const char *, int, const Target *);
local void Cuserid(const char *, int, const Target *);
local helptext_t usage_help, userid_help;


/* global data */

local Inet *net;
local Ichatnet *chatnet;
local Imainloop *ml;
local Ilogman *lm;
local Iconfig *cfg;
local Icmdman *cmd;
local Iplayerdata *pd;
local Ichat *chat;
local Imodman *mm;

local void (*CachedAuthDone)(int, AuthData*);
local int pendingrequests;
local PlayerData *players;

local int cfg_pingtime, cfg_serverid, cfg_groupid, cfg_scoreid;

/* protects pendingrequests, CachedAuthDone */
local pthread_mutex_t bigmtx;
#define LOCK() pthread_mutex_lock(&bigmtx)
#define UNLOCK() pthread_mutex_unlock(&bigmtx)

struct
{
	int usage, userid;
	short year, month, day, hour, minute, second;
} billing_data[MAXPLAYERS];


local Iauth _iauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "auth-billing", 5)
	BillingAuth
};

local Ibillcore _ibillcore =
{
	INTERFACE_HEAD_INIT(I_BILLCORE, "billcore-udp")
	SendToBiller, AddPacket, RemovePacket, GetStatus
};



EXPORT int MM_billcore(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!net || !ml || !cfg || !cmd || !chat) return MM_FAIL;

		pendingrequests = 0;
		CachedAuthDone = NULL;
		pthread_mutex_init(&bigmtx, NULL);

		players = pd->players;

		cfg_pingtime = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);
		cfg_serverid = cfg->GetInt(GLOBAL, "Billing", "ServerId", 5000),
		cfg_groupid = cfg->GetInt(GLOBAL, "Billing", "GroupId", 1),
		cfg_scoreid = cfg->GetInt(GLOBAL, "Billing", "ScoreId", 5000),

		ml->SetTimer(SendPing, 300, 3000, NULL);

		/* packets from billing server */
		AddPacket(0, SendLogin); /* sent from net when it's time to contact biller */
		AddPacket(B2S_PLAYERDATA, BAuthResponse);
		AddPacket(B2S_CHATMSG, BChatMsg);
		AddPacket(B2S_ZONEMESSAGE, BMessage);
		AddPacket(B2S_SINGLEMESSAGE, BSingleMessage);

		/* packets from clients */
		net->AddPacket(C2S_CHAT, PChat);
		if (chatnet) chatnet->AddHandler("SEND", MChat);

		cmd->AddCommand(NULL, DefaultCmd, NULL);
		cmd->AddCommand("userid", Cuserid, userid_help);
		cmd->AddCommand("usage", Cusage, usage_help);

		mm->RegInterface(&_iauth, ALLARENAS);
		mm->RegInterface(&_ibillcore, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		byte dis = S2B_LOGOFF;

		if (mm->UnregInterface(&_iauth, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&_ibillcore, ALLARENAS))
			return MM_FAIL;

		/* send logoff packet (immediate so it gets there before */
		/* connection drop) */
		SendToBiller(&dis, 1, NET_RELIABLE | NET_PRI_P4);
		net->DropClient(PID_BILLER);

		cmd->RemoveCommand(NULL, DefaultCmd);
		cmd->RemoveCommand("userid", Cuserid);
		cmd->RemoveCommand("usage", Cusage);

		RemovePacket(0, SendLogin);
		RemovePacket(B2S_PLAYERDATA, BAuthResponse);

		net->RemovePacket(C2S_CHAT, PChat);
		if (chatnet) chatnet->RemoveHandler("SEND", MChat);

		ml->ClearTimer(SendPing);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	return MM_FAIL;
}


void SendToBiller(byte *data, int length, int flags)
{
	net->SendToOne(PID_BILLER, data, length, flags);
}

void AddPacket(byte pktype, PacketFunc func)
{
	net->AddPacket(pktype + PKT_BILLER_OFFSET, func);
}

void RemovePacket(byte pktype, PacketFunc func)
{
	net->RemovePacket(pktype + PKT_BILLER_OFFSET, func);
}

int GetStatus(void)
{
	int st;
	pd->LockStatus();
	st = players[PID_BILLER].status;
	pd->UnlockStatus();
	return st;
}


int SendPing(void *dummy)
{
	int status;
	status = GetStatus();
	if (status == BNET_NOBILLING)
	{	/* no communication yet, send initiation packet */
		byte initiate[8] = { 0x00, 0x01, 0xDA, 0x8F, 0xFD, 0xFF, 0x01, 0x00 };
		SendToBiller(initiate, 8, NET_UNRELIABLE | NET_PRI_P3);
		lm->Log(L_INFO, "<billcore> Attempting to connect to billing server");
	}
	else if (status == BNET_CONNECTED)
	{	/* connection established, send ping */
		byte ping = S2B_KEEPALIVE;
		SendToBiller(&ping, 1, NET_RELIABLE | NET_PRI_P3);
	}
	return 1;
}


void SendLogin(int pid, byte *p, int n)
{
	struct S2BLogin to =
	{
		S2B_LOGIN, cfg_serverid, cfg_groupid, cfg_scoreid,
		"<default zone name>", "password"
	};
	const char *t;

	lm->Log(L_INFO, "<billcore> Billing server contacted, sending zone information");
	t = cfg->GetStr(GLOBAL, "Billing", "ServerName");
	if (t) astrncpy(to.name, t, 0x80);
	t = cfg->GetStr(GLOBAL, "Billing", "Password");
	if (t) astrncpy(to.pw, t, 0x20);
	SendToBiller((byte*)&to, sizeof(to), NET_RELIABLE);
}


void DefaultCmd(const char *cmd, int pid, const Target *target)
{
	struct S2BCommand *to;

	if (target->type == T_ARENA)
	{
		if (chat)
		{
			chat_mask_t mask = chat->GetPlayerChatMask(pid);
			if (IS_RESTRICTED(mask, MSG_BCOMMAND))
				return;
		}

		to = alloca(strlen(cmd)+7);
		to->type = S2B_COMMAND;
		to->pid = pid;
		to->text[0] = '?';
		strcpy(to->text+1, cmd);
		SendToBiller((byte*)to, strlen(cmd)+7, NET_RELIABLE);
	}
}


unsigned int get_ip(int pid)
{
	if (IS_STANDARD(pid))
	{
		struct net_client_stats stats;
		net->GetClientStats(pid, &stats);
		return inet_addr(stats.ipaddr);
	}
	else if (IS_CHAT(pid))
	{
		struct chat_client_stats stats;
		chatnet->GetClientStats(pid, &stats);
		return inet_addr(stats.ipaddr);
	}
	else
		return 0;
}


void BillingAuth(int pid, struct LoginPacket *lp, int lplen,
		void (*Done)(int, AuthData*))
{
	LOCK();

	if (pendingrequests >= CFG_MAX_PENDING_REQUESTS)
	{
		/* tell the client we're too busy */
		AuthData auth;
		memset(&auth, 0, sizeof(auth));
		auth.code = AUTH_SERVERBUSY;
		Done(pid, &auth);
	}
	else if (GetStatus() == BNET_CONNECTED)
	{
		struct S2BPlayerEntering to =
		{
			S2B_PLAYERLOGIN,
			lp->flags,
			0,
			"", "",
			pid,
			lp->macid,
			lp->timezonebias, 0
		};
		int len;

		to.ipaddy = get_ip(pid);
		astrncpy(to.name, lp->name, 32);
		astrncpy(to.pw, lp->password, 32);

		/* only send extra 64 bytes if they were supplied by the client */
		len = (lplen == LEN_LOGINPACKET_CONT) ? sizeof(to) : sizeof(to) - 64;
		SendToBiller((byte*)&to, len, NET_RELIABLE | NET_PRI_P3);

		CachedAuthDone = Done;

		pendingrequests++;
	}
	else
	{
		/* DEFAULT TO OLD AUTHENTICATION if billing server not available */
		AuthData auth;
		memset(&auth, 0, sizeof(auth));
		auth.code = AUTH_NOSCORES; /* tell client no scores kept */
		/* prepend names with ^ to indicate they're not authenticated */
		auth.name[0] = '^';
		astrncpy(auth.name+1, lp->name, 23);
		strncpy(auth.sendname, auth.name, 20);
		Done(pid, &auth);
		memset(billing_data + pid, 0, sizeof(billing_data[pid]));
	}

	UNLOCK();
}


void BAuthResponse(int bpid, byte *p, int n)
{
	struct B2SPlayerResponse *r = (struct B2SPlayerResponse *)p;
	AuthData ad;
	int pid = r->pid;

	memset(&ad, 0, sizeof(ad));
	/*ad.demodata = 0; // FIXME: figure out where in the billing response that is */
	ad.code = r->loginflag;
	astrncpy(ad.name, r->name, 24);
	strncpy(ad.sendname, ad.name, 20);
	astrncpy(ad.squad, r->squad, 24);

	/* all scores are local!
	if (n >= sizeof(struct B2SPlayerResponse))
	{
		players[pid].wins = ad.wins = r->wins;
		players[pid].losses = ad.losses = r->losses;
		players[pid].flagpoints = ad.flagpoints = r->flagpoints;
		players[pid].killpoints = ad.killpoints = r->killpoints;
	}
	*/

	LOCK();
	if (CachedAuthDone && pendingrequests > 0)
	{
		CachedAuthDone(pid, &ad);
		pendingrequests--;
	}
	else
		lm->Log(L_ERROR, "<billcore> Got billing response before request sent");
	UNLOCK();

#define DO(field) \
	billing_data[pid].field = r->field
	DO(usage);
	DO(year); DO(month); DO(day);
	DO(hour); DO(minute); DO(second);
#undef DO

	/* banner data handled in banner module */
}


void BChatMsg(int pid, byte *p, int len)
{
	struct B2SChat *from = (struct B2SChat*)p;
	int set[] = { from->uid, -1 };
	chat->SendAnyMessage(set, MSG_CHAT, 0, "%i:%s", from->channel, from->text);
}


/* this does remote privs as well as ** and *szone messages */
void BMessage(int pid, byte *p, int len)
{
	struct B2SZoneMessage *from = (struct B2SZoneMessage*)p;
	char *msg = from->text, *t;

	if (msg[0] == ':')
	{
		/* remote priv message */
		t = strchr(msg+1, ':');
		if (!t)
			/* no matching : */
			lm->Log(L_MALICIOUS, "<billcore> Malformed remote private message from biller");
		else
		{
			*t = 0;
			if (msg[1] == '#')
			{
				/* squad msg */
				int set[MAXPLAYERS+1], setc = 0, i;
				pd->LockStatus();
				for (i = 0; i < MAXPLAYERS; i++)
					if (	players[i].status == S_CONNECTED &&
							strcasecmp(msg+2, players[i].squad) == 0)
						set[setc++] = i;
				pd->UnlockStatus();
				set[setc] = -1;
				chat->SendAnyMessage(set, MSG_INTERARENAPRIV, 0, "%s", t+1);
			}
			else
			{
				int set[] = { -1, -1 };
				/* normal priv msg */
				set[0] = pd->FindPlayer(msg+1);
				if (PID_OK(set[0]))
					chat->SendAnyMessage(set, MSG_INTERARENAPRIV, 0, "%s", t+1);
			}
			*t = ':';
		}
	}
	else
	{
		/* ** or *szone message, send to all */
		int set[MAXPLAYERS+1];
		Target target;
		target.type = T_ZONE;

		lm->Log(L_WARN, "<billcore> Broadcast message from biller: %s", msg);

		pd->TargetToSet(&target, set);
		chat->SendSetMessage(set, 0, msg);
	}
}


void BSingleMessage(int pid, byte *p2, int len)
{
	struct S2BCommand *p = (struct S2BCommand*)p2;
	chat->SendMessage(p->pid, p->text);
}



local void handle_chat(int pid, const char *msg)
{
	chat_mask_t mask;
	int len = strlen(msg) + 38;
	struct S2BChat *to = alloca(len);

	mask = chat ? chat->GetPlayerChatMask(pid) : 0;
	if (IS_RESTRICTED(mask, MSG_CHAT))
		return;

	to->type = S2B_CHATMSG;
	to->pid = pid;

	if (strchr(msg, ';'))
	{
		delimcpy(to->channel, msg, 32, ';');
		len -= strlen(to->channel) + 1; /* don't send bytes after null */
		strcpy(to->text, strchr(msg, ';')+1);
	}
	else
	{
		memset(to->channel, 0, 32);
		strcpy(to->text, msg);
	}

	SendToBiller((byte*)to, len, NET_RELIABLE);
}


local void handle_priv(int pid, const char *msg)
{
	chat_mask_t mask;
	char *t;
	int l;
	struct S2BRemotePriv *to = alloca(strlen(msg) + 46); /* long enough for anything */

	mask = chat ? chat->GetPlayerChatMask(pid) : 0;
	if (IS_RESTRICTED(mask, MSG_INTERARENAPRIV))
		return;

	t = strchr(msg+1, ':');
	if (msg[0] != ':' || !t)
	{
		lm->Log(L_MALICIOUS,"<billcore> [%s] Malformed remote private message '%s'", players[pid].name, msg);
	}
	else
	{
		to->type = S2B_PRIVATEMSG;
		to->groupid = cfg_groupid;
		to->unknown1 = 2;
		*t = 0;
		l = sprintf(to->text, ":%s:(%s)>%s",
				msg+1,
				players[pid].name,
				t+1);
		SendToBiller((byte*)to, l+12, NET_RELIABLE | NET_PRI_P1);
		*t = ':';
	}
}


void PChat(int pid, byte *p, int len)
{
	struct ChatPacket *from = (struct ChatPacket *)p;

	if (from->type == MSG_CHAT)
		handle_chat(pid, from->text);
	else if (from->type == MSG_INTERARENAPRIV)
		handle_priv(pid, from->text);
}


void MChat(int pid, const char *line)
{
	const char *t;
	char subtype[10];

	t = delimcpy(subtype, line, sizeof(subtype), ':');
	if (!t) return;

	if (!strcasecmp(subtype, "CHAT"))
	{
		handle_chat(pid, t);
	}
	else if (!strcasecmp(subtype, "PRIV"))
	{
		int i;
		char name[24];
		t = delimcpy(name, t, sizeof(name), ':');
		if (!t) return;
		i = pd->FindPlayer(name);
		if (i == -1)
			handle_priv(pid, t - 1); /* t-1 to grab the initial colon */
	}
}


local helptext_t usage_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the usage information (current hours and minutes logged in, and\n"
"total hours and minutes logged in), as well as the first login time, of\n"
"the target player, or you if no target.\n";

void Cusage(const char *params, int pid, const Target *target)
{
	int mins, t;

	t = target->type == T_PID ? target->u.pid : pid;

	mins = (GTC() - players[t].connecttime) / 6000;

	chat->SendMessage(pid, "usage: %s:", players[t].name);
	chat->SendMessage(pid, "usage: session: %5d:%02d",
			mins / 60, mins % 60);
	mins += billing_data[t].usage;
	chat->SendMessage(pid, "usage:   total: %5d:%02d",
			mins / 60, mins % 60);
	chat->SendMessage(pid, "usage: first played: %d-%d-%d %d:%02d:%02d",
			billing_data[t].month,
			billing_data[t].day,
			billing_data[t].year,
			billing_data[t].hour,
			billing_data[t].minute,
			billing_data[t].second);
}


local helptext_t userid_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the billing server id of the target player, or yours if no\n"
"target.\n";

void Cuserid(const char *params, int pid, const Target *target)
{
	int t = target->type == T_PID ? target->u.pid : pid;
	chat->SendMessage(pid, "userid: %s has userid %d",
			players[t].name, billing_data[t].userid);
}


