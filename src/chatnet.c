
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

/* global clients struct! */
local cdata clients[MAXPLAYERS];
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
	int opts = fcntl(s, F_GETFL);
	if (opts == -1)
		return -1;
	else if (fcntl(s, F_SETFL, opts | O_NONBLOCK) == -1)
		return -1;
	else
		return 0;
}


local int init_socket(void)
{
	int s, port;
	struct sockaddr_in sin;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		Error(ERROR_GENERAL, "net: WSAStartup");
#endif

	port = cfg->GetInt(GLOBAL, "Net", "ChatPort", -1);
	if (port == -1)
		port = cfg->GetInt(GLOBAL, "Net", "Port", 5000) + 2;

	sin.sin_family = AF_INET;
	memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
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
local int try_accept(int s)
{
	int a, pid;
	socklen_t sinsize;
	struct sockaddr_in sin;

	sinsize = sizeof(sin);
	a = accept(s, &sin, &sinsize);

	if (a == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> accept() failed");
		return -1;
	}

	if (set_nonblock(a) == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> set_nonblock() failed");
		close(a);
		return -1;
	}

	pid = pd->NewPlayer(T_CHAT);

	lm->Log(L_DRIVEL, "<chatnet> [pid=%d] New connection from %s:%i",
			pid, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

	clients[pid].socket = a;
	clients[pid].sin = sin;
	clients[pid].inbuf = NULL;
	LLInit(&clients[pid].outbufs);

	return pid;
}


local void process_line(int pid, const char *line)
{
	char cmd[MAXCMDNAME + 1], *t;
	Link *l;
	LinkedList *lst;

	for (t = cmd; *line && *line != ':'; line++)
		if ((t - cmd) < MAXCMDNAME)
			*t++ = *line;
	*t = 0; /* terminate command name */
	if (*line == ':') line++; /* pass colon */

	lst = HashGet(handlers, cmd);

	for (l = LLGetHead(lst); l; l = l->next)
		((MessageFunc)(l->data))(pid, line);
}


/* call with lock held */
local void kill_connection(int pid)
{
	if (!IS_CHAT(pid))
		return;

	pd->LockPlayer(pid);

	if (pd->players[pid].arena >= 0)
		process_line(pid, "LEAVE");

	pd->LockStatus();

	/* make sure that he's on his way out, in case he was kicked before
	 * fully logging in. */
	if (pd->players[pid].status < S_LEAVING_ARENA)
		pd->players[pid].status = S_LEAVING_ZONE;

	/* set this special flag so that the player will be set to leave
	 * the zone when the S_LEAVING_ARENA-initiated actions are
	 * completed. */
	pd->players[pid].whenloggedin = S_LEAVING_ZONE;

	pd->UnlockStatus();
	pd->UnlockPlayer(pid);
}


/* call with big lock */
local void do_read(int pid)
{
	int n;
	buffer *buf = clients[pid].inbuf;
	char *src;

	if (!buf)
		buf = get_in_buffer();

	n = read(clients[pid].socket, buf->cur, LEFT(buf));

	if (n == 0)
	{
		kill_connection(pid);
		return;
	}

	if (n > 0)
		buf->cur += n;

	/* try processing stuff */
	src = buf->data;
	/* as long as there's a complete message... */
	while (memchr(src, CR, buf->cur - src) || memchr(src, LF, buf->cur - src))
	{
		char line[MAXMSGSIZE+1], *dst = line;
		/* copy it into line, minus terminators */
		while (*src != CR && *src != LF) *dst++ = *src++;
		/* close it off */
		*dst = 0;
		/* process it */
		process_line(pid, line);
		/* skip terminators in input */
		while (*src == CR || *src == LF) src++;
	}

	/* if we processed some data... */
	if (src > buf->data)
	{
		buffer *buf2 = NULL;
		/* and there's unprocessed data left... */
		if ((buf->cur - src) > 0)
		{
			/* put the unprocessed data in a new buffer */
			buffer *buf2 = get_in_buffer();
			memcpy(buf2->data, src, buf->cur - src);
			buf2->cur += buf->cur - src;
		}
		/* and free the old one */
		afree(buf);
		buf = buf2;
	}

	/* if the line is too long... */
	if (buf && LEFT(buf) <= 0)
	{
		lm->LogP(L_MALICIOUS, "chatnet", pid, "Line too long");
		afree(buf);
		buf = NULL;
	}

	/* replace the buffer */
	clients[pid].inbuf = buf;
}


/* call with big lock */
local void do_write(int pid)
{
	Link *l = LLGetHead(&clients[pid].outbufs);
	if (l && l->data)
	{
		buffer *buf = l->data;
		int n, len;

		len = strlen(buf->data) - (buf->cur - buf->data);

		n = write(clients[pid].socket, buf->cur, len);

		if (n > 0)
			buf->cur += n;

		/* check if this buffer is done */
		if (buf->cur[0] == 0)
			LLRemoveFirst(&clients[pid].outbufs);
	}
}


local int main_loop(void *dummy)
{
	int pid, max, ret;
	fd_set readset, writeset;
	struct timeval tv = { 0, 0 };

	FD_ZERO(&readset);
	FD_ZERO(&writeset);

	/* always listen for accepts on listening socket */
	FD_SET(mysock, &readset);
	max = mysock;

	LOCK();

	for (pid = 0; pid < MAXPLAYERS; pid++)
		if (IS_CHAT(pid) &&
		    pd->players[pid].status >= S_CONNECTED &&
		    clients[pid].socket > 2)
		{
			if (pd->players[pid].status != S_TIMEWAIT)
			{
				/* always check for more incoming data */
				FD_SET(clients[pid].socket, &readset);
				/* maybe for writing too */
				if (LLCount(&clients[pid].outbufs) > 0)
					FD_SET(clients[pid].socket, &writeset);
				/* update max */
				if (clients[pid].socket > max)
					max = clients[pid].socket;
			}
			else
			{
				/* handle disconnects */
				lm->LogP(L_INFO, "chatnet", pid, "Disconnected");
				close(clients[pid].socket);
				clients[pid].socket = -1;
				pd->FreePlayer(pid);
			}
		}

	ret = select(max + 1, &readset, &writeset, NULL, &tv);

	if (ret > 0)
	{
		/* new connections? */
		if (FD_ISSET(mysock, &readset))
			try_accept(mysock);

		for (pid = 0; pid < MAXPLAYERS; pid++)
			if (IS_CHAT(pid) &&
			    pd->players[pid].status >= S_CONNECTED &&
			    clients[pid].socket > 2)
			{
				/* data to read? */
				if (FD_ISSET(clients[pid].socket, &readset))
					do_read(pid);
				/* or write? */
				if (FD_ISSET(clients[pid].socket, &writeset))
					do_write(pid);
			}
	}

	UNLOCK();

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



local void real_send(int *set, const char *line, va_list ap)
{
	char buf[MAXMSGSIZE+1];

	vsnprintf(buf, MAXMSGSIZE - 2, line, ap);
	strcat(buf, "\x0D\x0A");

	LOCK();

	for ( ; *set != -1; set++)
		if (IS_CHAT(*set))
			LLAdd(&clients[*set].outbufs, get_out_buffer(buf));

	UNLOCK();
}


local void SendToSet(int *set, const char *line, ...)
{
	va_list args;
	va_start(args, line);
	real_send(set, line, args);
	va_end(args);
}


local void SendToOne(int pid, const char *line, ...)
{
	va_list args;
	int set[] = { pid, -1 };
	va_start(args, line);
	real_send(set, line, args);
	va_end(args);
}


local void SendToArena(int arena, int except, const char *line, ...)
{
	va_list args;
	int set[MAXPLAYERS+1], i, p = 0;

	if (arena < 0) return;

	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (pd->players[i].status == S_PLAYING &&
		    pd->players[i].arena == arena &&
		    i != except)
			set[p++] = i;
	pd->UnlockStatus();
	set[p] = -1;

	va_start(args, line);
	real_send(set, line, args);
	va_end(args);
}


local void GetClientStats(int pid, struct chat_client_stats *stats)
{
	if (!stats || PID_BAD(pid)) return;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(clients[pid].sin.sin_addr), 16);
	stats->port = clients[pid].sin.sin_port;
}



local Ichatnet _int =
{
	INTERFACE_HEAD_INIT(I_CHATNET, "net-chat")
	AddHandler, RemoveHandler,
	SendToOne, SendToArena, SendToSet,
	kill_connection,
	GetClientStats
};


EXPORT int MM_chatnet(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		/* get the sockets */
		mysock = init_socket();
		if (mysock == -1)
			return MM_FAIL;

		/* init mutx */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&bigmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		handlers = HashAlloc(71);

		/* install timer */
		ml->SetTimer(main_loop, 10, 10, NULL);

		/* install ourself */
		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(main_loop);

		/* release these */
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);

		/* clean up */
		HashFree(handlers);

		pthread_mutex_destroy(&bigmtx);

		close(mysock);

		return MM_OK;
	}
	return MM_FAIL;
}

