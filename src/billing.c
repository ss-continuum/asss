
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sched.h>
#else
#define close(a) closesocket(a)
#endif

#include "asss.h"
#include "protutil.h"


typedef struct pdata
{
	long billingid;
	unsigned long usage;
	char firstused[32];
	void (*Done)(Player *p, AuthData *data);
	int knowntobiller;
} pdata;


/* this holds the current status of our connection */
local enum
{
	s_no_socket,
	s_connecting,
	s_connected,
	s_waitlogin,
	s_loggedin,
	s_retry,
	s_loginfailed,
	s_disabled
} state;

local time_t lastretry;
local int pdkey;
local sp_conn conn;
local pthread_mutex_t mtx;

local int cfg_retryseconds;

local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Ichat *chat;
local Icmdman *cmd;
local Iconfig *cfg;
local Iauth *oldauth;


/* utility thingies */

local void memtohex(char *dest, byte *mem, int bytes)
{
	static const char tab[16] = "0123456789abcdef";
	int i;
	for (i = 0; i < bytes; i++)
	{
		*dest++ = tab[(mem[i]>>4) & 0x0f];
		*dest++ = tab[(mem[i]>>0) & 0x0f];
	}
	*dest = 0;
}

#if 0
local void hextomem(byte *dest, char *text, int bytes)
{
	int i;
	for (i = 0; i < bytes; i++)
	{
		byte d = 0;
		char c1 = *text++;
		char c2 = *text++;

		if (c1 >= '0' && c1 <= '9')
			d = c1 - '0';
		else if (c1 >= 'a' && c1 <= 'f')
			d = c1 - 'a' + 10;
		else if (c1 >= 'A' && c1 <= 'F')
			d = c1 - 'A' + 10;
		else return;

		d <<= 4;

		if (c2 >= '0' && c2 <= '9')
			d |= c2 - '0';
		else if (c2 >= 'a' && c2 <= 'f')
			d |= c2 - 'a' + 10;
		else if (c2 >= 'A' && c2 <= 'F')
			d |= c2 - 'A' + 10;
		else return;

		*dest++ = d;
	}
}
#endif

local void send_line(const char *line)
{
	pthread_mutex_lock(&mtx);
	sp_send(&conn, line);
	pthread_mutex_unlock(&mtx);
}


local void drop_connection(int newstate)
{
	Player *p;
	Link *link;
	pdata *data;

	/* clear knowntobiller values */
	pd->Lock();
	FOR_EACH_PLAYER_P(p, data, pdkey)
		data->knowntobiller = 0;
	pd->Unlock();

	/* then close socket */
	if (conn.socket > 0)
		close(conn.socket);
	conn.socket = -1;
	state = newstate;

}

/* the auth interface */

local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
			void (*Done)(Player *p, AuthData *data))
{
	char buf[MAXMSGSIZE];
	char ip[16] = "127.0.0.1";
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);

	if (state == s_loggedin)
	{
		data->Done = Done;

		snprintf(buf, MAXMSGSIZE - 128, "PLOGIN:%d:%d:%s:%s:%s:%d:",
				p->pid, lp->flags, lp->name, lp->password, ip, lp->macid);
		if (lplen == LEN_LOGINPACKET_CONT)
			memtohex(buf + strlen(buf), lp->contid, 64);

		sp_send(&conn, buf);

		data->knowntobiller = TRUE;
	}
	else
	{
		/* biller isn't connected, fall back to next highest priority */
		lm->Log(L_DRIVEL,
				"<billing> biller not connected; falling back to '%s'",
				oldauth->head.name);
		oldauth->Authenticate(p, lp, lplen, Done);
		data->knowntobiller = FALSE;
	}

	pthread_mutex_unlock(&mtx);
}

local struct Iauth myauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "billing-auth", 8)
	authenticate
};


/* catch players logging out */

local void paction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey);
	if (action == PA_DISCONNECT && data->knowntobiller)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "PLEAVE:%d", p->pid);
		send_line(buf);
	}
}


/* handle chat messages that have to go to the biller */

local void onchatmsg(Player *p, int type, int sound, Player *target, int freq, const char *text)
{
	pdata *data = PPDATA(p, pdkey);

	if (!data->knowntobiller)
		return;

	if (type == MSG_CHAT)
	{
		char buf[MAXMSGSIZE], chan[16];
		const char *t;

		t = strchr(text, ';');
		if (t && (t-text) < 10)
			text = delimcpy(chan, text, sizeof(chan), ';');
		else
			strcpy(chan, "1");

		snprintf(buf, sizeof(buf), "CHAT:%d:%s:%d:%s", p->pid, chan, sound, text);
		send_line(buf);
	}
	else if (type == MSG_REMOTEPRIV && target == NULL)
	{
		/* only grab these if the server didn't handle them internally */
		const char *t;
		char dest[32];

		t = delimcpy(dest, text+1, sizeof(dest), ':');
		if (text[0] != ':' || !t)
			lm->LogP(L_MALICIOUS, "billing", p, "malformed remote private message");
		else if (dest[0] == '#')
		{
			char buf[MAXMSGSIZE];
			snprintf(buf, sizeof(buf), "RMTSQD:%d:%s:%d:%s", p->pid, dest+1, sound, t);
			send_line(buf);
		}
		else
		{
			char buf[MAXMSGSIZE];
			snprintf(buf, sizeof(buf), "RMT:%d:%s:%d:%s", p->pid, dest, sound, t);
			send_line(buf);
		}
	}
}


/* and command that go to the biller */

local void Cdefault(const char *cmd, const char *params, Player *p, const Target *target)
{
	pdata *data = PPDATA(p, pdkey);
	char buf[MAXMSGSIZE];

	if (!data->knowntobiller)
		return;

	if (target->type != T_ARENA)
	{
		lm->LogP(L_DRIVEL, "billing", p, "unknown command with bad target: %s %s", cmd, params);
		return;
	}

	snprintf(buf, sizeof(buf), "CMD:%d:%s:%s", p->pid, cmd, params);
	send_line(buf);
}


/* other useful commands */

local helptext_t usage_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the usage information (current hours and minutes logged in, and\n"
"total hours and minutes logged in), as well as the first login time, of\n"
"the target player, or you if no target.\n";

local void Cusage(const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *tdata = PPDATA(t, pdkey);
	unsigned int mins, secs;

	if (tdata->knowntobiller)
	{
		secs = TICK_DIFF(current_ticks(), t->connecttime) / 100;
		mins = secs / 60;

		if (t != p) chat->SendMessage(p, "usage: %s:", t->name);
		chat->SendMessage(p, "session: %5d:%02d:%02d",
				mins / 60, mins % 60, secs % 60);
		secs += tdata->usage;
		mins = secs / 60;
		chat->SendMessage(p, "  total: %5d:%02d:%02d",
				mins / 60, mins % 60, secs % 60);
		chat->SendMessage(p, "first played: %s", tdata->firstused);
	}
	else
		chat->SendMessage(p, "usage unknown for %s", t->name);
}


local helptext_t billingid_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the billing server id of the target player, or yours if no\n"
"target.\n";

local void Cbillingid(const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *tdata = PPDATA(t, pdkey);
	if (tdata->knowntobiller)
		chat->SendMessage(p, "%s has billing id %ld",
				t->name, tdata->billingid);
	else
		chat->SendMessage(p, "billing id unknown for %s", t->name);
}


local helptext_t billingadm_help =
"Targets: none\n"
"Args: status|drop|connect\n"
"The subcommand 'status' reports the status of the billing server\n"
"connection. 'drop' disconnects the connection if it's up, and 'connect'\n"
"reconnects after dropping or failed login.\n";

local void Cbillingadm(const char *params, Player *p, const Target *target)
{
	pthread_mutex_lock(&mtx);

	if (!strcmp(params, "drop"))
	{
		/* if we're up, drop the socket */
		if (conn.socket > 0)
			drop_connection(s_disabled);
		else
			state = s_disabled;
		state = s_disabled;
		chat->SendMessage(p, "billing connection disabled");
	}
	else if (!strcmp(params, "connect"))
	{
		if (state == s_loginfailed || state == s_disabled)
		{
			state = s_no_socket;
			chat->SendMessage(p, "billing server connection reactivated");
		}
		else
			chat->SendMessage(p, "billing server connection already active");
	}
	else
	{
		const char *t = NULL;
		switch (state)
		{
			case s_no_socket:
				t = "not connected yet";  break;
			case s_connecting:
				t = "connecting";  break;
			case s_connected:
				t = "connected";  break;
			case s_waitlogin:
				t = "waiting for login response";  break;
			case s_loggedin:
				t = "logged in";  break;
			case s_retry:
				t = "waiting to retry";  break;
			case s_loginfailed:
				t = "disabled (login failed)";  break;
			case s_disabled:
				t = "disabled (by user)";  break;
		}
		chat->SendMessage(p, "billing status: %s", t);
	}

	pthread_mutex_unlock(&mtx);
}


/* handlers for all messages from the biller */

local void process_connectok(const char *line)
{
	lm->Log(L_INFO, "<billing> logged into billing server (%s)",
			line);
	state = s_loggedin;
}

local void process_connectbad(const char *line)
{
	char billername[128];
	const char *reason;

	reason = delimcpy(billername, line, sizeof(billername), ':');

	lm->Log(L_INFO, "<billing> billing server (%s) rejected login: %s",
			billername, reason);

	/* now close it and don't try again */
	drop_connection(s_loginfailed);
}

local void process_pok(const char *line)
{
	/* b->g: "POK:pid:rtext:name:squad:billingid:usage:firstused" */
	AuthData ad;
	char pidstr[16], rtext[256], bidstr[16], usagestr[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;
	t = delimcpy(rtext, t, sizeof(rtext), ':');
	if (!t) return;
	t = delimcpy(ad.name, t, 21, ':');
	if (!t) return;
	strncpy(ad.sendname, ad.name, 20);
	t = delimcpy(ad.squad, t, sizeof(ad.squad), ':');
	if (!t) return;
	t = delimcpy(bidstr, t, sizeof(bidstr), ':');
	if (!t) return;
	t = delimcpy(usagestr, t, sizeof(usagestr), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		pdata *data = PPDATA(p, pdkey);
		astrncpy(data->firstused, t, sizeof(data->firstused));
		data->usage = atol(usagestr);
		data->billingid = atol(bidstr);
		ad.demodata = 0;
		ad.code = AUTH_OK;
		ad.authenticated = TRUE;
		data->Done(p, &ad);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player auth response for unknown pid %s", pidstr);
}

local void process_pbad(const char *line)
{
	/* b->g: "PBAD:pid:rtext" */
	AuthData ad;
	char pidstr[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		pdata *data = PPDATA(p, pdkey);
		memset(&ad, 0, sizeof(ad));
		/* ew.. i really wish i didn't have to do this */
		if (!strncmp(t, "CODE", 4))
			ad.code = atoi(t+4);
		else
		{
			ad.code = AUTH_CUSTOMTEXT;
			astrncpy(ad.customtext, t, sizeof(ad.customtext));
		}
		data->Done(p, &ad);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player auth response for unknown pid %s", pidstr);
}

local void process_bnr(const char *line)
{
	/* b->g: "BNR:pid:banner" */
	/* FIXME: banner support */
}

static struct
{
	char channel[32];
	char sender[32];
	int sound;
	char text[256];
} chatdata;

local void process_chattxt(const char *line)
{
	/* b->g: "CHATTXT:channel:sender:sound:text" */
	char soundstr[16];
	const char *t = line;

	t = delimcpy(chatdata.channel, t, sizeof(chatdata.channel), ':');
	if (!t) return;
	t = delimcpy(chatdata.sender, t, sizeof(chatdata.sender), ':');
	if (!t) return;
	t = delimcpy(soundstr, t, sizeof(soundstr), ':');
	if (!t) return;
	chatdata.sound = atoi(soundstr);
	astrncpy(chatdata.text, t, sizeof(chatdata.text));
}

local void process_chat(const char *line)
{
	/* b->g: "CHAT:pid:number" */
	char pidstr[16], num[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;
	t = delimcpy(num, t, sizeof(num), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		Link link = { NULL, p };
		LinkedList list = { &link, &link };

		chat->SendAnyMessage(&list, MSG_CHAT, chatdata.sound, NULL,
				"%s:%s> %s", num, chatdata.sender, chatdata.text);
	}
}

local void process_rmt(const char *line)
{
	/* b->g: "RMT:pid:sender:sound:text" */
	char pidstr[16], sender[32], soundstr[16];
	const char *text = line;
	Player *p;

	text = delimcpy(pidstr, text, sizeof(pidstr), ':');
	if (!text) return;
	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		Link link = { NULL, p };
		LinkedList list = { &link, &link };

		chat->SendAnyMessage(&list, MSG_REMOTEPRIV, atoi(soundstr), NULL,
				"%s> %s", sender, text);
	}
}

local void process_rmtsqd(const char *line)
{
	/* b->g: "RMTSQD:destsquad:sender:sound:text" */
	char destsq[32], sender[32], soundstr[16];
	const char *t = line;

	LinkedList list = LL_INITIALIZER;
	Link *link;
	Player *p;

	t = delimcpy(destsq, t, sizeof(destsq), ':');
	if (!t) return;
	t = delimcpy(sender, t, sizeof(sender), ':');
	if (!t) return;
	t = delimcpy(soundstr, t, sizeof(soundstr), ':');
	if (!t) return;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (strcmp(destsq, p->squad) == 0)
			LLAdd(&list, p);
	pd->Unlock();

	chat->SendAnyMessage(&list, MSG_REMOTEPRIV, atoi(soundstr), NULL,
			"S (%s)> %s", sender, t);
}

local void process_msg(const char *line)
{
	/* b->g: "MSG:pid:sound:text" */
	char pidstr[16], soundstr[16];
	const char *text = line;
	Player *p;

	text = delimcpy(pidstr, text, sizeof(pidstr), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
		chat->SendSoundMessage(p, atoi(soundstr), "%s", text);
}

local void process_staffmsg(const char *line)
{
	/* b->g: "STAFFMSG:sender:sound:text" */
	char sender[32], soundstr[16];
	const char *text = line;

	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	chat->SendModMessage("Staff message from billing server> %s", text);
}

local void process_broadcast(const char *line)
{
	/* b->g: "BROADCAST:sender:sound:text" */
	char sender[32], soundstr[16];
	const char *text = line;

	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	chat->SendArenaSoundMessage(ALLARENAS, atoi(soundstr),
			"Broadcast from billing server> %s", text);
}


/* the dispatcher */

local void process_line(const char *cmd, const char *rest, void *dummy)
{
	     if (!strcmp(cmd, "CONNECTOK"))      process_connectok(rest);
	else if (!strcmp(cmd, "CONNECTBAD"))     process_connectbad(rest);
	else if (!strcmp(cmd, "POK"))            process_pok(rest);
	else if (!strcmp(cmd, "PBAD"))           process_pbad(rest);
	else if (!strcmp(cmd, "BNR"))            process_bnr(rest);
	else if (!strcmp(cmd, "CHATTXT"))        process_chattxt(rest);
	else if (!strcmp(cmd, "CHAT"))           process_chat(rest);
	else if (!strcmp(cmd, "RMT"))            process_rmt(rest);
	else if (!strcmp(cmd, "RMTSQD"))         process_rmtsqd(rest);
	else if (!strcmp(cmd, "MSG"))            process_msg(rest);
	else if (!strcmp(cmd, "STAFFMSG"))       process_staffmsg(rest);
	else if (!strcmp(cmd, "BROADCAST"))      process_broadcast(rest);
}


/* stuff to handle connecting */

local void setup_proxy(const char *proxy, const char *ipaddr, int port)
{
	/* this means we're using an external proxy to connect to the
	 * biller. set up some sockets and fork it off */
	int sockets[2], r;
	pid_t chld;

	r = socketpair(PF_UNIX, SOCK_STREAM, 0, sockets);
	if (r < 0)
	{
		lm->Log(L_ERROR, "<billing> socketpair failed: %s", strerror(errno));
		state = s_disabled;
		return;
	}

	/* no need to bother with pthread_atfork handlers because we're just
	 * going to exec the proxy immediately */
	chld = fork();

	if (chld < 0)
	{
		lm->Log(L_ERROR, "<billing> fork failed: %s", strerror(errno));
		state = s_disabled;
		return;
	}
	else if (chld == 0)
	{
		/* in child */
		char portstr[16];

		/* set up fds, but leave stdout connected to stdout of the server */
		close(sockets[1]);
		dup2(sockets[0], STDIN_FILENO);
		dup2(sockets[0], STDOUT_FILENO);
		close(sockets[0]);

		snprintf(portstr, sizeof(portstr), "%d", port);
		execlp(proxy, proxy, ipaddr, portstr, NULL);

		/* uh oh */
		fprintf(stderr, "E <billing> can't exec billing proxy (%s): %s\n",
				proxy, strerror(errno));
		exit(123);
	}
	else
	{
		/* in parent */
		close(sockets[0]);
		set_nonblock(sockets[1]);
		conn.socket = sockets[1];
		/* skip right over s_connecting */
		state = s_connected;
	}
}


local void remote_connect(const char *ipaddr, int port)
{
	int r;
	struct sockaddr_in sin;

	conn.socket = init_client_socket();

	if (conn.socket == -1)
	{
		state = s_retry;
		return;
	}

	lm->Log(L_INFO, "<billing> trying to connect to billing server at %s:%d",
			ipaddr, port);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(ipaddr);
	r = connect(conn.socket, (struct sockaddr*)&sin, sizeof(sin));

	if (r == 0)
	{
		/* successful connect. this is pretty unlikely since the socket
		 * is nonblocking. */
		lm->Log(L_INFO, "<billing> connected to billing server");
		state = s_connected;
	}
	else if (errno == EINPROGRESS)
	{
		/* this is the most likely result */
		state = s_connecting;
	}
	else
	{
		lm->Log(L_WARN, "<billing> unexpected error from connect: %s",
				strerror(errno));
		/* retry again in a while */
		state = s_retry;
	}
}


local void get_socket(void)
{
	/* cfghelp: Billing:Proxy, global, string
	 * This setting allows you to specify an external program that will
	 * handle the billing server connection. The program should be
	 * prepared to speak the asss billing protocol over its standard
	 * input and output. It will get two command line arguments, which
	 * are the ip and port of the billing server, as specified in the
	 * Billing:IP and Billing:Port settings. The program name should
	 * either be an absolute pathname or be located on your $PATH.
	 */
	const char *proxy = cfg->GetStr(GLOBAL, "Billing", "Proxy");
	/* cfghelp: Billing:IP, global, string
	 * The ip address of the billing server (no dns hostnames allowed). */
	const char *ipaddr = cfg->GetStr(GLOBAL, "Billing", "IP");
	/* cfghelp: Billing:Port, global, int, def: 1850
	 * The port to connect to on the billing server. */
	int port = cfg->GetInt(GLOBAL, "Billing", "Port", 1850);

	lastretry = time(NULL);

	if (proxy)
		setup_proxy(proxy, ipaddr, port);
	else
		remote_connect(ipaddr, port);
}


local void check_connected(void)
{
	/* we have an connect in progress. check it. */

	fd_set fds;
	struct timeval tv = { 0, 0 };
	int r;

	FD_ZERO(&fds);
	FD_SET(conn.socket, &fds);
	r = select(conn.socket + 1, NULL, &fds, NULL, &tv);

	if (r > 0)
	{
		/* we've got a result */
		int opt;
		socklen_t optlen = sizeof(opt);

		r = getsockopt(conn.socket, SOL_SOCKET, SO_ERROR, &opt, &optlen);

		if (r < 0)
			lm->Log(L_WARN, "<billing> unexpected error from getsockopt: %s",
					strerror(errno));
		else
		{
			if (opt == 0)
			{
				/* successful connection */
				lm->Log(L_INFO, "<billing> connected to billing server");
				state = s_connected;
			}
			else
			{
				lm->Log(L_WARN, "<billing> can't connect to billing server: %s",
						strerror(opt));
				state = s_retry;
			}
		}
	}
	else if (r < 0)
	{
		/* error from select */
		lm->Log(L_WARN, "<billing> unexpected error from select: %s",
				strerror(errno));
		/* abort and retry in a while */
		drop_connection(s_retry);
	}
}


local void try_login(void)
{
	char buf[MAXMSGSIZE];

	/* cfghelp: Billing:ServerName, global, string
	 * The server name to send to the billing server. */
	const char *zonename = cfg->GetStr(GLOBAL, "Billing", "ServerName");
	/* cfghelp: Billing:ServerNetwork, global, string
	 * The network name to send to the billing server. A network name
	 * should identify a group of servers (e.g., SSCX). */
	const char *net = cfg->GetStr(GLOBAL, "Billing", "ServerNetwork");
	/* cfghelp: Billing:Password, global, string
	 * The password to log in to the billing server with. */
	const char *pwd = cfg->GetStr(GLOBAL, "Billing", "Password");

	if (!zonename) zonename = "";
	if (!net) net = "";
	if (!pwd) pwd = "";

	snprintf(buf, sizeof(buf), "CONNECT:1:asss "ASSSVERSION":%s:%s:%s",
			zonename, net, pwd);
	sp_send(&conn, buf);

	state = s_waitlogin;
}


/* runs often */

local void try_send_recv(void)
{
	fd_set rfds, wfds;
	struct timeval tv = { 0, 0 };

	if (conn.socket < 0) return;

	FD_ZERO(&rfds);
	FD_SET(conn.socket, &rfds);
	FD_ZERO(&wfds);
	if (!LLIsEmpty(&conn.outbufs))
		FD_SET(conn.socket, &wfds);

	select(conn.socket+1, &rfds, &wfds, NULL, &tv);

	if (FD_ISSET(conn.socket, &rfds))
		if (do_sp_read(&conn) == sp_read_died)
		{
			/* lost connection */
			lm->Log(L_INFO, "<billing> lost connection to billing server (read eof)");
			drop_connection(s_retry);
			return;
		}

	if (FD_ISSET(conn.socket, &wfds))
		do_sp_write(&conn);

	if (conn.inbuf)
		do_sp_process(&conn, process_line, NULL);
}


local int do_one_iter(void *v)
{
	pthread_mutex_lock(&mtx);
	if (state == s_no_socket)
		get_socket();
	else if (state == s_connecting)
		check_connected();
	else if (state == s_connected)
		try_login();
	else if (state == s_waitlogin || state == s_loggedin)
		try_send_recv();
	else if (state == s_retry)
		if ( (time(NULL) - lastretry) > cfg_retryseconds)
			state = s_no_socket;
	pthread_mutex_unlock(&mtx);

	return TRUE;
}



EXPORT int MM_billing(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		if (!pd || !lm || !ml || !chat || !cmd || !cfg || !oldauth)
			return MM_FAIL;
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		conn.socket = -1;
		drop_connection(s_no_socket);

		/* cfghelp: Billing:RetryInterval, global, int, def: 30
		 * How many seconds to wait between tries to connect to the
		 * billing server. */
		cfg_retryseconds = cfg->GetInt(GLOBAL, "Billing", "RetryInterval", 30);

		pthread_mutex_init(&mtx, NULL);

		ml->SetTimer(do_one_iter, 10, 10, NULL, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_CHATMSG, onchatmsg, ALLARENAS);

		cmd->AddCommand("usage", Cusage, usage_help);
		cmd->AddCommand("billingid", Cbillingid, billingid_help);
		cmd->AddCommand("billingadm", Cbillingadm, billingadm_help);
		cmd->AddCommand2(NULL, Cdefault, NULL);

		mm->RegInterface(&myauth, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;

		cmd->RemoveCommand("usage", Cusage);
		cmd->RemoveCommand("billingid", Cbillingid);
		cmd->RemoveCommand("billingadm", Cbillingadm);
		cmd->RemoveCommand2(NULL, Cdefault);
		mm->UnregCallback(CB_CHATMSG, onchatmsg, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		ml->ClearTimer(do_one_iter, NULL);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(oldauth);
		return MM_OK;
	}
	else
		return MM_FAIL;
}


