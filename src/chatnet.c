
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>

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


/* locking notes: we only acquire the big chatnet lock in many places
 * when checking player type information because although new players
 * may be created and destroyed without that mutex, chatnet players may
 * not be, and those are the only ones we're interested in here. */


#define MAXCMDNAME 16
#define MAXMSGSIZE 1023

#define CR 0x0D
#define LF 0x0A



/* defines */

typedef struct buffer
{
	char *cur; /* points to within data */
	char data[1];
} buffer;

#define LEFT(buf) (MAXMSGSIZE - ((buf)->cur - (buf)->data) - 1)

typedef struct cdata
{
	int socket;
	struct sockaddr_in sin;
	unsigned int lastmsgtime;
	buffer *inbuf;
	LinkedList outbufs;
} cdata;

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Iconfig *cfg;

local HashTable *handlers;
local int mysock;
local int cfg_msgdelay;
local int cdkey;
local pthread_mutex_t bigmtx;
#define LOCK() pthread_mutex_lock(&bigmtx)
#define UNLOCK() pthread_mutex_unlock(&bigmtx)


local buffer *get_out_buffer(const char *str)
{
	buffer *b = amalloc(sizeof(buffer) + strlen(str));
	strcpy(b->data, str);
	b->cur = b->data;
	return b;
}

local buffer *get_in_buffer(void)
{
	buffer *b = amalloc(sizeof(buffer) + MAXMSGSIZE);
	b->cur = b->data;
	return b;
}


local int set_nonblock(int s)
{
#ifndef WIN32
	int opts = fcntl(s, F_GETFL);
	if (opts == -1)
		return -1;
	else if (fcntl(s, F_SETFL, opts | O_NONBLOCK) == -1)
		return -1;
	else
		return 0;
#else
	unsigned long nb = 1;

	if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR)
		return -1;
	else
		return 0;
#endif
}


local int init_socket(void)
{
	int s, port;
	struct sockaddr_in sin;
	unsigned long int bindaddr;
	const char *addr;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		return -1;
#endif

	/* cfghelp: Net:ChatPort, global, int, def: Net:Port + 2, \
	 * mod: chatnet
	 * The port that the text-based chat protocol runs on. */
	port = cfg->GetInt(GLOBAL, "Net", "ChatPort", -1);
	if (port == -1)
		port = cfg->GetInt(GLOBAL, "Net", "Port", 5000) + 2;
	/* cfghelp: Net:ChatBindIP, global, string, def: Net:BindIP, \
	 * mod: chatnet
	 * If this is set, it must be a single IP address that the server
	 * should bind to for the text-based chat protocol. If unset, it
	 * will use the value of Net:BindIP. */
	addr = cfg->GetStr(GLOBAL, "Net", "ChatBindIP");
	if (!addr) addr = cfg->GetStr(GLOBAL, "Net", "BindIP");
	bindaddr = addr ? inet_addr(addr) : INADDR_ANY;

	sin.sin_family = AF_INET;
	memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_addr.s_addr = bindaddr;
	sin.sin_port = htons(port);

	s = socket(PF_INET, SOCK_STREAM, 0);

	if (s == -1)
	{
		perror("socket");
		return -1;
	}

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) == -1)
	{
		perror("bind");
		close(s);
		return -1;
	}

	if (set_nonblock(s) == -1)
	{
		perror("set_nonblock");
		close(s);
		return -1;
	}

	if (listen(s, 5) == -1)
	{
		perror("listen");
		close(s);
		return -1;
	}

	return s;
}


/* call with big lock */
local Player * try_accept(int s)
{
	Player *p;
	cdata *cli;

	int a;
	socklen_t sinsize;
	struct sockaddr_in sin;

	sinsize = sizeof(sin);
	a = accept(s, (struct sockaddr*)&sin, &sinsize);

	if (a == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> accept() failed");
		return NULL;
	}

	if (set_nonblock(a) == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> set_nonblock() failed");
		close(a);
		return NULL;
	}

	p = pd->NewPlayer(T_CHAT);

	lm->Log(L_DRIVEL, "<chatnet> [pid=%d] New connection from %s:%i",
			p->pid, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

	cli = PPDATA(p, cdkey);
	cli->socket = a;
	cli->sin = sin;
	cli->lastmsgtime = 0;
	cli->inbuf = NULL;
	LLInit(&cli->outbufs);

	return p;
}


local void process_line(Player *p, const char *line)
{
	char cmd[MAXCMDNAME + 1], *t;
	Link *l;
	LinkedList lst = LL_INITIALIZER;

	for (t = cmd; *line && *line != ':'; line++)
		if ((t - cmd) < MAXCMDNAME)
			*t++ = *line;
	*t = 0; /* terminate command name */
	if (*line == ':') line++; /* pass colon */

	HashGetAppend(handlers, cmd, &lst);

	for (l = LLGetHead(&lst); l; l = l->next)
		((MessageFunc)(l->data))(p, line);

	LLEmpty(&lst);
}


/* call with lock held */
local void clear_bufs(Player *p)
{
	cdata *cli = PPDATA(p, cdkey);
	afree(cli->inbuf);
	cli->inbuf = 0;
	LLEnum(&cli->outbufs, afree);
	LLEmpty(&cli->outbufs);
}

/* call with lock held */
local void kill_connection(Player *p)
{
	if (!IS_CHAT(p)) return;

	clear_bufs(p);

	pd->LockPlayer(p);

	/* will put in S_LEAVING_ARENA */
	if (p->arena)
		process_line(p, "LEAVE");

	pd->WriteLock();

	/* make sure that he's on his way out, in case he was kicked before
	 * fully logging in. */
	if (p->status < S_LEAVING_ARENA)
		p->status = S_LEAVING_ZONE;

	/* set this special flag so that the player will be set to leave
	 * the zone when the S_LEAVING_ARENA-initiated actions are
	 * completed. */
	p->whenloggedin = S_LEAVING_ZONE;

	pd->Unlock();
	pd->UnlockPlayer(p);
}


/* call with big lock */
local void do_read(Player *p)
{
	cdata *cli = PPDATA(p, cdkey);
	buffer *buf = cli->inbuf;
	int n;

	if (!buf)
		buf = get_in_buffer();

	n = read(cli->socket, buf->cur, LEFT(buf));

	if (n == 0)
	{
		/* client disconnected */
		kill_connection(p);
		return;
	}
	else if (n > 0)
		buf->cur += n;

	/* if the line is too long... */
	if (LEFT(buf) <= 0)
	{
		lm->LogP(L_MALICIOUS, "chatnet", p, "Line too long");
		afree(buf);
		buf = NULL;
	}

	/* replace the buffer */
	cli->inbuf = buf;
}


/* call w/big lock */
local void try_process(Player *p)
{
	cdata *cli = PPDATA(p, cdkey);
	buffer *buf = cli->inbuf;
	char *src = buf->data;

	/* if there is a complete message */
	if (memchr(src, CR, buf->cur - src) || memchr(src, LF, buf->cur - src))
	{
		char line[MAXMSGSIZE+1], *dst = line;
		buffer *buf2 = NULL;

		/* copy it into line, minus terminators */
		while (*src != CR && *src != LF) *dst++ = *src++;
		/* close it off */
		*dst = 0;

		/* process it */
		process_line(p, line);

		/* skip terminators in input */
		while (*src == CR || *src == LF) src++;
		/* if there's unprocessed data left... */
		if ((buf->cur - src) > 0)
		{
			/* put the unprocessed data in a new buffer */
			buf2 = get_in_buffer();
			memcpy(buf2->data, src, buf->cur - src);
			buf2->cur += buf->cur - src;
		}

		/* free the old one */
		afree(buf);
		/* put the new one in place */
		cli->inbuf = buf2;
		/* reset message time */
		cli->lastmsgtime = GTC();
	}
}


/* call with big lock */
local void do_write(Player *p)
{
	cdata *cli = PPDATA(p, cdkey);
	Link *l = LLGetHead(&cli->outbufs);

	if (l && l->data)
	{
		buffer *buf = l->data;
		int n, len;

		len = strlen(buf->data) - (buf->cur - buf->data);

		n = write(cli->socket, buf->cur, len);

		if (n > 0)
			buf->cur += n;

		/* check if this buffer is done */
		if (buf->cur[0] == 0)
		{
			afree(buf);
			LLRemoveFirst(&cli->outbufs);
		}
	}
}


local int main_loop(void *dummy)
{
	Player *p;
	cdata *cli;
	Link *link;
	int max, ret, gtc = GTC();
	fd_set readset, writeset;
	struct timeval tv = { 0, 0 };
	LinkedList toremove = LL_INITIALIZER;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);

	/* always listen for accepts on listening socket */
	FD_SET(mysock, &readset);
	max = mysock;

	LOCK();

	pd->Lock();
	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p) &&
		    p->status >= S_CONNECTED &&
		    cli->socket > 2)
		{
			if (p->status != S_TIMEWAIT)
			{
				/* always check for incoming data */
				FD_SET(cli->socket, &readset);
				/* maybe for writing too */
				if (LLCount(&cli->outbufs) > 0)
					FD_SET(cli->socket, &writeset);
				/* update max */
				if (cli->socket > max)
					max = cli->socket;
			}
			else
			{
				/* handle disconnects */
				lm->LogP(L_INFO, "chatnet", p, "Disconnected");
				close(cli->socket);
				cli->socket = -1;
				/* we can't remove players while we're iterating through
				 * the list, so add them and do them later. */
				LLAdd(&toremove, p);
			}
		}
	pd->Unlock();

	ret = select(max + 1, &readset, &writeset, NULL, &tv);

	/* new connections? */
	if (FD_ISSET(mysock, &readset))
		try_accept(mysock);

	pd->Lock();
	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p) &&
		    p->status < S_TIMEWAIT &&
		    cli->socket > 2)
		{
			/* data to read? */
			if (FD_ISSET(cli->socket, &readset))
				do_read(p);
			/* or write? */
			if (FD_ISSET(cli->socket, &writeset))
				do_write(p);
			/* or process? */
			if (cli->inbuf &&
			    (gtc - cli->lastmsgtime) > cfg_msgdelay)
				try_process(p);
		}
	pd->Unlock();

	UNLOCK();

	/* remove players that disconnected above */
	for (link = LLGetHead(&toremove); link; link = link->next)
		pd->FreePlayer(link->data);
	LLEmpty(&toremove);

	return TRUE;
}


local void AddHandler(const char *type, MessageFunc f)
{
	LOCK();
	HashAdd(handlers, type, f);
	UNLOCK();
}

local void RemoveHandler(const char *type, MessageFunc f)
{
	LOCK();
	HashRemove(handlers, type, f);
	UNLOCK();
}



local void real_send(LinkedList *lst, const char *line, va_list ap)
{
	Link *l;
	char buf[MAXMSGSIZE+1];

	vsnprintf(buf, MAXMSGSIZE - 2, line, ap);
	strcat(buf, "\x0D\x0A");

	LOCK();
	for (l = LLGetHead(lst); l; l = l->next)
	{
		Player *p = l->data;
		cdata *cli = PPDATA(p, cdkey);
		if (IS_CHAT(p))
			LLAdd(&cli->outbufs, get_out_buffer(buf));
	}
	UNLOCK();
}


local void SendToSet(LinkedList *set, const char *line, ...)
{
	va_list args;
	va_start(args, line);
	real_send(set, line, args);
	va_end(args);
}


local void SendToOne(Player *p, const char *line, ...)
{
	va_list args;

	/* this feels a bit wrong */
	Link l = { NULL, p };
	LinkedList lst = { &l, &l };

	va_start(args, line);
	real_send(&lst, line, args);
	va_end(args);
}


local void SendToArena(Arena *arena, Player *except, const char *line, ...)
{
	va_list args;
	LinkedList lst = LL_INITIALIZER;
	Link *link;
	Player *p;

	if (!arena) return;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && p->arena == arena && p != except)
			LLAdd(&lst, p);
	pd->Unlock();

	va_start(args, line);
	real_send(&lst, line, args);
	va_end(args);

	LLEmpty(&lst);
}


local void GetClientStats(Player *p, struct chat_client_stats *stats)
{
	cdata *cli = PPDATA(p, cdkey);
	if (!stats || !p) return;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(cli->sin.sin_addr), 16);
	stats->port = cli->sin.sin_port;
}


local void do_final_shutdown(void)
{
	Link *link;
	Player *p;
	cdata *cli;

	LOCK();
	pd->Lock();
	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p))
		{
			/* try to clean up as much memory as possible */
			clear_bufs(p);
			/* close all the connections also */
			if (cli->socket > 2)
				close(cli->socket);
		}
	pd->Unlock();
	UNLOCK();
}


local Ichatnet _int =
{
	INTERFACE_HEAD_INIT(I_CHATNET, "net-chat")
	AddHandler, RemoveHandler,
	SendToOne, SendToArena, SendToSet,
	kill_connection,
	GetClientStats
};


EXPORT int MM_chatnet(int action, Imodman *mm_, Arena *a)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!pd || !cfg || !lm || !ml) return MM_FAIL;

		cdkey = pd->AllocatePlayerData(sizeof(cdata));
		if (cdkey == -1) return MM_FAIL;

		/* get the sockets */
		mysock = init_socket();
		if (mysock == -1) return MM_FAIL;

		/* cfghelp: Net:ChatMessageDelay, global, int, def: 20 \
		 * mod: chatnet
		 * The delay between sending messages to clients using the
		 * text-based chat protocol. (To limit bandwidth used by
		 * non-playing cilents.) */
		cfg_msgdelay = cfg->GetInt(GLOBAL, "Net", "ChatMessageDelay", 20);

		/* init mutex */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&bigmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		handlers = HashAlloc(71);

		/* install timer */
		ml->SetTimer(main_loop, 10, 10, NULL, NULL);

		/* install ourself */
		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(main_loop, NULL);

		/* clean up */
		do_final_shutdown();
		HashFree(handlers);
		pthread_mutex_destroy(&bigmtx);
		close(mysock);
		pd->FreePlayerData(cdkey);

		/* release these */
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	return MM_FAIL;
}

