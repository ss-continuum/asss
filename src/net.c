
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sched.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "asss.h"

/* SYNCHRONIZATION DEBUGGING
#define LockMutex(mtx) \
	log->Log(LOG_DEBUG, "waiting on " #mtx " at %i", __LINE__); \
	pthread_mutex_lock(mtx); \
	log->Log(LOG_DEBUG, "    locked " #mtx " at %i", __LINE__)

#define UnlockMutex(mtx) \
	log->Log(LOG_DEBUG, " unlocking " #mtx " at %i", __LINE__); \
	pthread_mutex_unlock(mtx)
*/

/* MACROS */

#define MAXTYPES 128
#define MAXENCRYPT 4

/* size of ip/port hash table */
#define HASHSIZE 256

#define NET_INPRESIZE 2
#define NET_INBIGPKT 4

/* STRUCTS */

#include "packets/reliable.h"

#include "packets/timesync.h"

typedef struct ClientData
{
	int s2cn, c2sn, flags, nextinbucket;
	struct sockaddr_in sin;
	unsigned int lastpkt, key;
	short enctype;
	int bigpktsize, bigpktroom;
	byte *bigpktbuf;
	DQNode outlist;
} ClientData;


/* retries, lastretry: only valid for buffers in outlist
 * pid, len: valid for all buffers
 */

typedef struct Buffer
{
	DQNode node;
	int retries, pid, len;
	unsigned int lastretry;
	union
	{
		struct ReliablePacket rel;
		byte raw[MAXPACKET];
	} d;
} Buffer;


/* PROTOTYPES */

/* interface: */
local void SendToOne(int, byte *, int, int);
local void SendToArena(int, int, byte *, int, int);
local void SendToSet(int *, byte *, int, int);
local void SendToAll(byte *, int, int);
local void ProcessPacket(int, byte *, int);
local void AddPacket(byte, PacketFunc);
local void RemovePacket(byte, PacketFunc);
local int NewConnection(struct sockaddr_in *);
local i32 GetIP(int);

/* internal: */
local inline int HashIP(struct sockaddr_in);
local inline void SendRaw(int, byte *, int);
local void KillConnection(int pid);
local void BufferPacket(int, byte *, int, int);
local void ProcessBuffer(Buffer *);
local void InitSockets(void);
local Buffer * GetBuffer(void);
local void FreeBuffer(Buffer *);

/* threads: */
local void * RecvThread(void *);
local void * SendThread(void *);
local void * RelThread(void *);

/* network layer header handling: */
local void ProcessKey(Buffer *);
local void ProcessKeyResponse(Buffer *);
local void ProcessReliable(Buffer *);
local void ProcessGrouped(Buffer *);
local void ProcessAck(Buffer *);
local void ProcessSyncRequest(Buffer *);
local void ProcessBigData(Buffer *);
local void ProcessPresize(Buffer *);
local void ProcessDrop(Buffer *);




/* GLOBAL DATA */

local Iplayerdata *pd;
local Ilogman *log;
local Iconfig *cfg;

local PlayerData *players;

local Iencrypt *crypters[MAXENCRYPT];

local LinkedList handlers[MAXTYPES];
local int mysock, myothersock, mybillingsock;

local DQNode freelist, rellist;
local Mutex freemtx, relmtx;
local Condition relcond;
volatile int killallthreads = 0;

/* global clients struct!: */
local ClientData clients[MAXPLAYERS+EXTRA_PID_COUNT];
local int clienthash[HASHSIZE];
local Mutex hashmtx;

local struct
{
	int port, retries, timeout, selectusec, process;
	int biglimit, usebilling, droptimeout, billping;
	int encmode, bufferdelta;
} config;

local struct
{
	int pcountpings, buffercount, pktssent, pktsrecvd;
} global_stats;

local void (*oohandlers[])(Buffer*) =
{
	NULL, /* 00 - nothing */
	ProcessKey, /* 01 - key initiation */
	ProcessKeyResponse, /* 02 - key response (to be used for billing server) */
	ProcessReliable, /* 03 - reliable */
	ProcessAck, /* 04 - reliable response */
	ProcessSyncRequest, /* 05 - time sync request */
	NULL, /* 06 - time sync response (possible anti-spoof) */
	ProcessDrop, /* 07 - close connection */
	ProcessBigData, /* 08 - bigpacket */
	ProcessBigData, /* 09 - bigpacket2 */
	ProcessPresize, /* 0A - presized bigdata */
	NULL, /* 0B - nothing */
	NULL, /* 0C - nothing */
	NULL, /* 0D - nothing */
	ProcessGrouped /* 0E - grouped */
};

local Inet _int =
{
	SendToOne, SendToArena, SendToSet, SendToAll,
	KillConnection, ProcessPacket, AddPacket, RemovePacket,
	NewConnection, GetIP
};



/* START OF FUNCTIONS */


int MM_net(int action, Imodman *mm, int arena)
{
	int i;

	if (action == MM_LOAD)
	{
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		if (!cfg || !log) return MM_FAIL;

		players = pd->players;

		for (i = 0; i < MAXTYPES; i++)
			LLInit(handlers + i);
		for (i = 0; i < MAXENCRYPT; i++)
			mm->RegInterest(I_ENCRYPTBASE + i, crypters + i);

		/* store configuration params */
		config.port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
		config.retries = cfg->GetInt(GLOBAL, "Net", "ReliableRetries", 5);
		config.timeout = cfg->GetInt(GLOBAL, "Net", "ReliableTimeout", 150);
		config.selectusec = cfg->GetInt(GLOBAL, "Net", "SelectUSec", 10000);
		config.process = cfg->GetInt(GLOBAL, "Net", "ProcessGroup", 5);
		config.biglimit = cfg->GetInt(GLOBAL, "Net", "BigLimit", 1);
		config.encmode = cfg->GetInt(GLOBAL, "Net", "EncryptMode", 0);
		config.droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		config.bufferdelta = cfg->GetInt(GLOBAL, "Net", "MaxBufferDelta", 15);
		config.usebilling = cfg->GetInt(GLOBAL, "Billing", "UseBilling", 0);
		config.billping = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);

		/* init hash and outlists */
		for (i = 0; i < HASHSIZE; i++)
			clienthash[i] = -1;
		for (i = 0; i < MAXPLAYERS + EXTRA_PID_COUNT; i++)
		{
			clients[i].nextinbucket = -1;
			DQInit(&clients[i].outlist);
		}
		InitMutex(&hashmtx);

		/* init buffers */
		InitCondition(&relcond);
		InitMutex(&freemtx); InitMutex(&relmtx);
		DQInit(&freelist); DQInit(&rellist);

		/* get the sockets */
		InitSockets();

		/* start the threads */
		StartThread(RecvThread, NULL);
		StartThread(SendThread, NULL);
		StartThread(RelThread, NULL);

		/* install ourself */
		mm->RegInterface(I_NET, &_int);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterface(I_NET, &_int);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		for (i = 0; i < MAXENCRYPT; i++)
			mm->UnregInterest(I_ENCRYPTBASE + i, crypters + i);

		for (i = 0; i < MAXTYPES; i++)
			LLEmpty(handlers + i);

		/* let threads die */
		killallthreads = 1;
		/* note: we don't join them because they could be blocked on
		 * something, and who ever wants to unload net anyway? */

		close(mysock);
		close(myothersock);
		if (config.usebilling) close(mybillingsock);

		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void AddPacket(byte t, PacketFunc f)
{
	if (t >= MAXTYPES) return;
	LLAdd(handlers+t, f);
}

void RemovePacket(byte t, PacketFunc f)
{
	if (t >= MAXTYPES) return;
	LLRemove(handlers+t, f);
}


int HashIP(struct sockaddr_in sin)
{
	register unsigned ip = sin.sin_addr.s_addr;
	register unsigned short port = sin.sin_port;
	return ((port>>1) ^ (ip) ^ (ip>>23) ^ (ip>>17)) & (HASHSIZE-1);
}


i32 GetIP(int pid)
{
	return clients[pid].sin.sin_addr.s_addr;
}


Buffer * GetBuffer(void)
{
	DQNode *dq;

	LockMutex(&freemtx);
	dq = freelist.prev;
	if (dq == &freelist)
	{
		/* no buffers left, alloc one */
		UnlockMutex(&freemtx);
		dq = amalloc(sizeof(Buffer));
		DQInit(dq);
		global_stats.buffercount++;
	}
	else
	{
		/* grab one off free list */
		DQRemove(dq);
		UnlockMutex(&freemtx);
		/* clear it after releasing mtx */
	}
	memset(dq + 1, 0, sizeof(Buffer) - sizeof(DQNode));
	return (Buffer *)dq;
}


void FreeBuffer(Buffer *dq)
{
	LockMutex(&freemtx);
	DQAdd(&freelist, (DQNode*)dq);
	UnlockMutex(&freemtx);
}


void InitSockets(void)
{
	struct sockaddr_in localsin;

	localsin.sin_family = AF_INET;
	memset(localsin.sin_zero,0,sizeof(localsin.sin_zero));
	localsin.sin_addr.s_addr = htonl(INADDR_ANY);
	localsin.sin_port = htons(config.port);

	if ((mysock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_GENERAL,"net: socket");
	if (bind(mysock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_BIND,"net: bind");

	localsin.sin_port = htons(config.port+1);
	if ((myothersock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_GENERAL,"net: socket");
	if (bind(myothersock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_BIND,"net: bind");

	if (config.usebilling)
	{
		/* get socket */
		if ((mybillingsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
			Error(ERROR_GENERAL, "could not allocate billing socket");

		/* set up billing client struct */
		memset(players + PID_BILLER, 0, sizeof(PlayerData));
		memset(clients + PID_BILLER, 0, sizeof(ClientData));
		players[PID_BILLER].status = S_CONNECTED;
		strcpy(players[PID_BILLER].name, "<<Billing Server>>");
		clients[PID_BILLER].c2sn = -1;
		clients[PID_BILLER].sin.sin_family = AF_INET;
		clients[PID_BILLER].sin.sin_addr.s_addr =
			inet_addr(cfg->GetStr(GLOBAL, "Billing", "IP"));
		clients[PID_BILLER].sin.sin_port = 
			htons(cfg->GetInt(GLOBAL, "Billing", "Port", 1850));
	}
}


void * RecvThread(void *dummy)
{
	struct sockaddr_in sin;
	struct timeval tv;
	fd_set fds;
	int len, pid, sinsize, type, maxfd = 5, hashbucket, n;

	while (!killallthreads)
	{
		do {
			/* set up fd set and tv */
			FD_ZERO(&fds);
			if (config.usebilling)
			{
				FD_SET(mybillingsock, &fds);
				if (mybillingsock > maxfd) maxfd = mybillingsock;
			}
			FD_SET(myothersock, &fds); if (myothersock > maxfd) maxfd = myothersock;
			FD_SET(mysock, &fds); if (mysock > maxfd) maxfd = mysock;

			tv.tv_sec = 10;
			tv.tv_usec = 0;

			/* perform select */
		} while (select(maxfd+1, &fds, NULL, NULL, &tv) < 1 && !killallthreads);

		/* first handle the main socket */
		if (FD_ISSET(mysock, &fds))
		{
			int status;
			Buffer *buf;

			buf = GetBuffer();
			sinsize = sizeof(sin);
			len = recvfrom(mysock, buf->d.raw, MAXPACKET, 0,
					(struct sockaddr*)&sin, &sinsize);

			if (len < 1)
			{
				FreeBuffer(buf);
				goto donehere;
			}

			buf->len = len;

			/* search for an existing connection */
			hashbucket = HashIP(sin);
			/* LOCK: lock status here? */
			LockMutex(&hashmtx);
			pid = clienthash[hashbucket];
			while (pid >= 0)
			{
				if (    players[pid].status > S_FREE
					 && clients[pid].sin.sin_addr.s_addr == sin.sin_addr.s_addr
					 && clients[pid].sin.sin_port == sin.sin_port)
					break;
				pid = clients[pid].nextinbucket;
			}
			UnlockMutex(&hashmtx);

			if (pid == -1)
			{	/* new client */
				pid = NewConnection(&sin);
				if (pid == -1)
				{
					byte pk7[] = { 0x00, 0x07 };
					sendto(mysock, pk7, 2, 0, (struct sockaddr*)&sin, sinsize);
					log->Log(L_WARN,"<net> Too many players! Dropping extra connections!");
					FreeBuffer(buf);
					goto donehere;
				}
				else
				{
					log->Log(L_DRIVEL,"<net> [pid=%d] New connection from %s:%i",
							pid, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
				}
			}

			/* check that it's in a reasonable status */
			status = players[pid].status;
			if (status <= S_FREE || status >= S_TIMEWAIT)
			{
				if (status <= S_FREE)
					log->Log(L_WARN, "<net> [pid=%d] Packet recieved from bad state %d", pid, status);
				if (status >= S_TIMEWAIT)
					log->Log(L_WARN, "<net> [pid=%d] Packet recieved from timewait state", pid);
				FreeBuffer(buf);
				goto donehere;
				/* don't set lastpkt time for timewait stats or we might
				 * never get out of it */
			}

			buf->pid = pid;
			/* set the last packet time */
			clients[pid].lastpkt = GTC();

			/* decrypt the packet */
			type = clients[pid].enctype;
			if (type >= 0 && type < MAXENCRYPT && crypters[type])
			{
				if (buf->d.rel.t1 == 0x00)
					crypters[type]->Decrypt(pid, buf->d.raw+2, len-2);
				else
					crypters[type]->Decrypt(pid, buf->d.raw+1, len-1);
			}

			/* hand it off to appropriate place */
			ProcessBuffer(buf);

			global_stats.pktsrecvd++;
donehere:
		}

		if (FD_ISSET(myothersock, &fds))
		{	/* data on port + 1 */
			unsigned int data[2];

			sinsize = sizeof(sin);
			n = recvfrom(myothersock, (char*)data, 4, 0,
					(struct sockaddr*)&sin, &sinsize);

			if (n == 4)
			{
				data[1] = data[0];
				data[0] = 0;
				pd->LockStatus();
				for (n = 0; n < MAXPLAYERS; n++)
					if (players[n].status == S_PLAYING)
						data[0]++;
				pd->UnlockStatus();
				sendto(myothersock, (char*)data, 8, 0,
						(struct sockaddr*)&sin, sinsize);
				global_stats.pcountpings++;
			}
		}

		if (FD_ISSET(mybillingsock, &fds))
		{	/* data from billing server */
			Buffer *buf;

			buf = GetBuffer();
			sinsize = sizeof(sin);
			n = recvfrom(mybillingsock, buf->d.raw, MAXPACKET, 0,
					(struct sockaddr*)&sin, &sinsize);
			if (memcmp(&sin, &clients[PID_BILLER].sin, sinsize))
				log->Log(L_MALICIOUS,
						"<net> Data recieved on billing server socket from incorrect origin: %s:%i",
						inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
			else if (n > 0)
			{
				clients[PID_BILLER].lastpkt = GTC();
				ProcessBuffer(buf);
			}
		}
	}
	return NULL;
}


void * SendThread(void *dummy)
{
	byte gbuf[MAXPACKET];
	unsigned int gtc, i;

	while (!killallthreads)
	{
		usleep(10000);

		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
		{
			int sentbig, pcount;
			DQNode *outlist;
			Buffer *buf, *nbuf;
			byte *gptr;

			/* set up context */
			sentbig = 0;
			pcount = 0;
			gptr = gbuf; *gptr++ = 0x00; *gptr++ = 0x0E;

			/* now lock player for as long as we are using his outlist */
			pd->LockPlayer(i);
			gtc = GTC();

			/* iterate through outlist */
			outlist = &clients[i].outlist;
			for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
			{
				nbuf = (Buffer*)buf->node.next;

				/* we don't have to remove packets that the client has
				 * acked because they shouldn't be here at all. */

				/* check status here */
				if (players[i].status <= S_FREE ||
				    players[i].status >= S_TIMEWAIT)
					buf->retries = 0;

				/* try to send it */
				if (buf->retries > 0 &&
				    (gtc - buf->lastretry) > config.timeout)
				{
					if (buf->len > 240)
					{ /* too big for grouped, send immediately */
						if (sentbig < config.biglimit)
						{
							SendRaw(buf->pid, buf->d.raw, buf->len);
							buf->lastretry = gtc;
							buf->retries--;
							sentbig = 1;
						}
					}
					else
					{ /* add to current grouped packet, if there is room */
						if (((gptr - gbuf) + buf->len + 10) < MAXPACKET)
						{
							*gptr++ = buf->len;
							memcpy(gptr, buf->d.raw, buf->len);
							gptr += buf->len;
							buf->lastretry = gtc;
							buf->retries--;
							pcount++;
						}
					}
				}

				/* free if possible */
				if (buf->retries < 1)
				{
					DQRemove((DQNode*)buf);
					FreeBuffer(buf);
				}
			}

			/* try sending the grouped packet */
			if (pcount == 1)
			{
				/* there's only one in the group, so don't send it
				 * in a group. +3 to skip past the 00 0E and size of
				 * first packet */
				SendRaw(i, gbuf + 3, (gptr - gbuf) - 3);
			}
			else if (pcount > 1)
			{
				/* send the whole thing as a group */
				SendRaw(i, gbuf, gptr - gbuf);
			}

			pd->UnlockPlayer(i);
		}

		/* process lagouts and timewait */
		/* do this in another loop so that we only have to lock/unlock
		 * player status once instead of MAXPLAYERS times around the
		 * loop. */
		pd->LockStatus();
		gtc = GTC();
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
		{
			/* this is used for lagouts and also for timewait */
			int diff = (int)gtc - (int)clients[i].lastpkt;

			/* process lagouts */
			if (   players[i].status != S_FREE
				&& players[i].whenloggedin == 0 /* acts as flag to prevent dups */
				&& clients[i].lastpkt != 0 /* prevent race */
				&& diff > config.droptimeout)
			{
				log->Log(L_DRIVEL,
						"<net> [%s] Player kicked for no data (lagged off)",
						players[i].name);
				/* FIXME: send "you have been disconnected..." msg */
				/* can't hold lock here for deadlock-related reasons */
				pd->UnlockStatus();
				KillConnection(i);
				pd->LockStatus();
			}

			/* process timewait states
			 * this is done with two states to ensure a complete pass
			 * through the outgoing buffer before marking these pids as
			 * free */
			if (players[i].status == S_TIMEWAIT2 && diff > 500)
			{
				/* remove from hash table. we do this now so that
				 * packets that arrive after the connection is closed
				 * (because of udp misorderings) don't cause a new
				 * connection to be created (at least for a short while)
				 */
				int bucket;
				LockMutex(&hashmtx);
				bucket = HashIP(clients[i].sin);
				if (clienthash[bucket] == i)
					clienthash[bucket] = clients[i].nextinbucket;
				else
				{
					int j = clienthash[bucket];
					while (j >= 0 && clients[j].nextinbucket != i)
						j = clients[j].nextinbucket;
					if (j >= 0)
						clients[j].nextinbucket = clients[i].nextinbucket;
					else
						log->Log(L_ERROR, "<net> Internal error: established connection not in hash table");
				}

				players[i].status = S_FREE;

				UnlockMutex(&hashmtx);
			}
			if (players[i].status == S_TIMEWAIT)
			{
				/* here, send disconnection packet */
				char drop[2] = {0x00, 0x07};
				SendToOne(i, drop, 2, NET_UNRELIABLE | NET_IMMEDIATE);
				players[i].status = S_TIMEWAIT2;
			}
		}
		pd->UnlockStatus();

	}
	return NULL;
#undef MAXGROUPED
}


void * RelThread(void *dummy)
{
	Buffer *buf, *nbuf;

	while (!killallthreads)
	{
		/* wait for reliable pkt to process */
		LockMutex(&relmtx);
		while (rellist.next == &rellist)
			WaitCondition(&relcond, &relmtx);

		for (buf = (Buffer*)rellist.next; (DQNode*)buf != &rellist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;

			/* if player is gone, free buffer */
			if (   players[buf->pid].status <= S_FREE
			    || players[buf->pid].status >= S_TIMEWAIT )
			{
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
				continue;
			}

			/* else, if seqnum matches, process */
			if (buf->d.rel.seqnum == (clients[buf->pid].c2sn + 1) )
			{
				clients[buf->pid].c2sn++;
				DQRemove((DQNode*)buf);
				/* don't hold mutex while processing packet */
				UnlockMutex(&relmtx);

				/* process it */
				ProcessPacket(buf->pid, buf->d.rel.data, buf->len - 6);

				FreeBuffer(buf);
				LockMutex(&relmtx);
			}
		}
		UnlockMutex(&relmtx);
	}
	return NULL;
}

/* ProcessBuffer
 * unreliable packets will be processed before the call returns and freed
 * network packets will be processed by the appropriate network handler,
 * which may free it or not.
 */
void ProcessBuffer(Buffer *buf)
{
	if (buf->d.rel.t1 == 0x00)
	{
		if (buf->d.rel.t2 < sizeof(oohandlers) && oohandlers[(int)buf->d.rel.t2])
			(oohandlers[(int)buf->d.rel.t2])(buf);
		else
		{
			log->Log(L_MALICIOUS, "<net> [%s] [pid=%d] Unknown network subtype %d",
					players[buf->pid].name,
					buf->pid,
					buf->d.rel.t2);
			FreeBuffer(buf);
		}
	}
	else if (buf->d.rel.t1 < PKT_BILLBASE)
	{
		LinkedList *lst = handlers+((int)buf->d.rel.t1);
		Link *l;

		if (buf->pid == PID_BILLER)
			lst = handlers+(buf->d.rel.t1 + PKT_BILLBASE);

		pd->LockPlayer(buf->pid);
		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)l->data)(buf->pid, buf->d.raw, buf->len);
		pd->UnlockPlayer(buf->pid);

		FreeBuffer(buf);
	}
}

/* ProcessPacket
 * regular packets (without headers) will be processed before the call
 * returns.
 * network packets will have a buffer created for them and will be
 * processed by ProcessBuffer, which will process them before the call
 * returns.
 */
void ProcessPacket(int pid, byte *d, int len)
{
	if (d[0] == 0x00 && len < MAXPACKET)
	{
		Buffer *buf;
		buf = GetBuffer();
		buf->pid = pid;
		buf->len = len;
		memcpy(buf->d.raw, d, len);
		ProcessBuffer(buf);
	}
	else if (d[0] < PKT_BILLBASE)
	{
		LinkedList *lst = handlers+d[0];
		Link *l;

		if (pid == PID_BILLER)
			lst = handlers+(d[0] + PKT_BILLBASE);

		pd->LockPlayer(pid);
		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)l->data)(pid, d, len);
		pd->UnlockPlayer(pid);
	}
}


int NewConnection(struct sockaddr_in *sin)
{
	int i = 0, bucket;

	pd->LockStatus();
	while (players[i].status != S_FREE && i < MAXPLAYERS) i++;
	pd->UnlockStatus();

	if (i == MAXPLAYERS) return -1;

	pd->LockPlayer(i);

	{ /* free any buffers remaining in the outlist */
		Buffer *buf, *nbuf;
		DQNode *outlist = &clients[i].outlist;
		for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;
			DQRemove((DQNode*)buf);
			FreeBuffer(buf);
		}
	}
	/* set up clientdata */
	memset(clients + i, 0, sizeof(ClientData));
	clients[i].c2sn = -1;
	clients[i].enctype = -1;
	DQInit(&clients[i].outlist);

	/* add him to his hash bucket */
	LockMutex(&hashmtx);
	if (sin)
	{
		memcpy(&clients[i].sin, sin, sizeof(struct sockaddr_in));
		bucket = HashIP(*sin);
		clients[i].nextinbucket = clienthash[bucket];
		clienthash[bucket] = i;
	}
	else
	{
		clients[i].nextinbucket = -1;
		clients[i].flags = NET_FAKE;
	}
	UnlockMutex(&hashmtx);

	/* set up playerdata */
	pd->LockStatus();
	memset(players + i, 0, sizeof(PlayerData));
	players[i].type = S2C_PLAYERENTERING; /* restore type */
	players[i].arena = -1;
	players[i].pid = i;
	players[i].shiptype = SPEC;
	players[i].attachedto = -1;
	players[i].status = S_NEED_KEY;
	pd->UnlockStatus();

	pd->UnlockPlayer(i);

	return i;
}


void KillConnection(int pid)
{
	int type;
	byte leaving = C2S_LEAVING;

	pd->LockPlayer(pid);

	/* if we haven't processed the leaving arena packet yet (quite
	 * likely), just generate one and process it. this will set status
	 * to S_LEAVING_ARENA */
	if (players[pid].arena >= 0)
		ProcessPacket(pid, &leaving, 1);

	pd->LockStatus();

	/* make sure that he's on his way out, in case he was kicked before
	 * fully logging in. */
	if (players[pid].status < S_LEAVING_ARENA)
		players[pid].status = S_LEAVING_ZONE;

	/* set status */
	if (pid == PID_BILLER)
	{
		log->Log(L_WARN, "<net> Connection to billing server lost");
		/* for normal players, ProcessLoginQueue runs and changes
		 * S_LEAVING_ZONE's to S_TIMEWAIT's after calling callback
		 * functions. but it doesn't do that for biller, so we set
		 * S_TIMEWAIT directly right here. */
		players[pid].status = S_TIMEWAIT;
		pd->UnlockStatus();
		pd->UnlockPlayer(pid);
		return;
	}
	else
	{
		/* set this special flag so that the player will be set to leave
		 * the zone when the S_LEAVING_ARENA-initiated actions are
		 * completed. */
		players[pid].whenloggedin = S_LEAVING_ZONE;
	}

	pd->UnlockStatus();
	pd->UnlockPlayer(pid);

	/* tell encryption to forget about him */
	type = clients[pid].enctype;
	if (type >= 0 && type < MAXENCRYPT)
		crypters[type]->Void(pid);

	/* log message */
	log->Log(L_INFO, "<net> [%s] [pid=%d] Disconnected",
			players[pid].name, pid);
}


void ProcessKey(Buffer *buf)
{
	int key = buf->d.rel.seqnum;
	short type = *(short*)buf->d.rel.data;
	PlayerData *player = players + buf->pid;

	pd->LockStatus();
	if (player->status != S_NEED_KEY)
	{
		pd->UnlockStatus();
		log->Log(L_MALICIOUS, "<net> [pid=%d] initiated key exchange from incorrect stage: %d, dropping", buf->pid, player->status);
		KillConnection(buf->pid);
		FreeBuffer(buf);
		return;
	}

	player->status = S_CONNECTED;
	pd->UnlockStatus();

	buf->d.rel.t2 = 2;

	if (config.encmode == 0)
	{
		SendRaw(buf->pid, buf->d.raw, 6);
	}
	else if (type >= 0 && type < MAXENCRYPT && crypters[type])
	{
		key = crypters[type]->Respond(key);
		buf->d.rel.seqnum = key;
		SendRaw(buf->pid, buf->d.raw, 6);
		crypters[type]->Init(buf->pid, key);
		clients[buf->pid].enctype = type;
	}
	else
		log->Log(L_MALICIOUS, "<net> [pid=%d] Unknown encryption type attempted to connect: %d", buf->pid, type);

	FreeBuffer(buf);
}


void ProcessKeyResponse(Buffer *buf)
{
	if (buf->pid < MAXPLAYERS)
		log->Log(L_MALICIOUS, "<net> [pid=%d] Key response from client", buf->pid);
	else
	{
		Link *l;

		players[buf->pid].status = BNET_CONNECTED;

		for (l = LLGetHead(handlers+(PKT_BILLBASE + 0));
				l; l = l->next)
			((PacketFunc)l->data)(buf->pid, buf->d.raw, buf->len);
	}
	FreeBuffer(buf);
}


void ProcessReliable(Buffer *buf)
{
	/* calculate seqnum delta to decide if we want to ack it. relmtx
	 * protects the c2sn values in the clients array. */
	int sn = buf->d.rel.seqnum;

	LockMutex(&relmtx);
	if ( (sn - clients[buf->pid].c2sn) <= config.bufferdelta)
	{
		/* ack. small hack. */
		buf->d.rel.t2++;
		SendRaw(buf->pid, buf->d.raw, 6);
		buf->d.rel.t2--;

		/* add to rel list to be processed */
		DQAdd(&rellist, (DQNode*)buf);
		UnlockMutex(&relmtx);
		SignalCondition(&relcond, 0);
	}
	else /* drop it */
	{
		UnlockMutex(&relmtx);
		FreeBuffer(buf);
		log->Log(L_DRIVEL, "<net> [%s] [pid=%d] Reliable packet with too big delta (%d - %d)",
				players[buf->pid].name, buf->pid,
				sn, clients[buf->pid].c2sn);
	}
}


void ProcessGrouped(Buffer *buf)
{
	int pos = 2, len = 1;

	while (pos < buf->len && len > 0)
	{
		len = buf->d.raw[pos++];
		ProcessPacket(buf->pid, buf->d.raw + pos, len);
		pos += len;
	}
	FreeBuffer(buf);
}


void ProcessAck(Buffer *buf)
{
	Buffer *b, *nbuf;
	DQNode *outlist;

	pd->LockPlayer(buf->pid);
	outlist = &clients[buf->pid].outlist;
	for (b = (Buffer*)outlist->next; (DQNode*)b != outlist; b = nbuf)
	{
		nbuf = (Buffer*)b->node.next;
		if (b->d.rel.t1 == 0x00 &&
		    b->d.rel.t2 == 0x03 &&
		    b->d.rel.seqnum == buf->d.rel.seqnum)
		{
			DQRemove((DQNode*)b);
			FreeBuffer(b);
		}
	}
	pd->UnlockPlayer(buf->pid);
	FreeBuffer(buf);
}


void ProcessSyncRequest(Buffer *buf)
{
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)(buf->d.raw);
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time, GTC() };
	BufferPacket(buf->pid, (byte*)&ts, sizeof(ts), NET_UNRELIABLE);
	FreeBuffer(buf);
}


void ProcessDrop(Buffer *buf)
{
	KillConnection(buf->pid);
	FreeBuffer(buf);
}


void ProcessBigData(Buffer *buf)
{
	int pid, newsize;
	byte *newbuf;

	pid = buf->pid;
	newsize = clients[buf->pid].bigpktsize + buf->len - 2;

	if (clients[pid].flags & NET_INPRESIZE)
	{
		log->Log(L_MALICIOUS, "<net> [%s] Recieved bigpacket while handling presized data",
				players[pid].name);
		goto reallyexit;
	}

	clients[pid].flags |= NET_INBIGPKT;

	if (newsize > MAXBIGPACKET)
	{
		log->Log(L_MALICIOUS,
			"<net> [%s] Big packet: refusing to allocate more than %d bytes",
			players[pid].name,
			MAXBIGPACKET);
		goto freebigbuf;
	}

	if (clients[pid].bigpktroom < newsize)
	{
		clients[pid].bigpktroom *= 2;
		if (clients[pid].bigpktroom < newsize) clients[pid].bigpktroom = newsize;
		newbuf = realloc(clients[pid].bigpktbuf, clients[pid].bigpktroom); 
		if (!newbuf)
		{
			log->Log(L_ERROR,"<net> [%s] Cannot allocate %d bytes for bigpacket",
				newsize, players[pid].name);
			goto freebigbuf;
		}
		clients[pid].bigpktbuf = newbuf;
	}
	else
		newbuf = clients[pid].bigpktbuf;

	memcpy(newbuf + clients[pid].bigpktsize, buf->d.raw + 2, buf->len - 2);

	clients[pid].bigpktbuf = newbuf;
	clients[pid].bigpktsize = newsize;

	if (buf->d.rel.t2 == 0x08) goto reallyexit;

	ProcessPacket(pid, newbuf, newsize);

freebigbuf:
	afree(clients[pid].bigpktbuf);
	clients[pid].bigpktbuf = NULL;
	clients[pid].bigpktsize = 0;
	clients[pid].bigpktroom = 0;
	clients[pid].flags &= ~NET_INBIGPKT;
reallyexit:
	FreeBuffer(buf);
}


void ProcessPresize(Buffer *buf)
{
	int size = buf->d.rel.seqnum, pid = buf->pid;

	if (clients[pid].flags & NET_INBIGPKT)
	{
		log->Log(L_MALICIOUS,"<net> [%s] Recieved presized data while handling bigpacket", players[pid].name);
		goto reallyexit;
	}

	if (clients[pid].bigpktbuf)
	{	/* copy data */
		if (size != clients[pid].bigpktroom)
		{
			log->Log(L_MALICIOUS, "<net> [%s] Presized data length mismatch", players[pid].name);
			goto freepacket;
		}
		memcpy(clients[pid].bigpktbuf+clients[pid].bigpktsize, buf->d.rel.data, buf->len - 6);
		clients[pid].bigpktsize += (buf->len - 6);
	}
	else
	{	/* allocate it */
		if (size > MAXBIGPACKET)
		{
			log->Log(L_MALICIOUS,
				"<net> [%s] Big packet: refusing to allocate more than %d bytes",
				players[pid].name,
				MAXBIGPACKET);
		}
		else
		{	/* copy initial segment	 */
			clients[pid].bigpktbuf = amalloc(size);
			clients[pid].bigpktroom = size;
			memcpy(clients[pid].bigpktbuf, buf->d.rel.data, buf->len - 6);
			clients[pid].bigpktsize = buf->len - 6;
		}
	}
	if (clients[pid].bigpktsize < size) return;

	ProcessPacket(pid, clients[pid].bigpktbuf, size);

freepacket:
	afree(clients[pid].bigpktbuf);
	clients[pid].bigpktbuf = NULL;
	clients[pid].bigpktsize = 0;
	clients[pid].bigpktroom = 0;
	clients[pid].flags &= ~NET_INPRESIZE;
reallyexit:
	FreeBuffer(buf);
}


void SendRaw(int pid, byte *data, int len)
{
	byte encbuf[MAXPACKET];
	int type = clients[pid].enctype;

	if (clients[pid].flags & NET_FAKE) return;

	if (pid == PID_BILLER)
	{
		sendto(mybillingsock, data, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));
	}
	else
	{
		memcpy(encbuf, data, len);

		if (type >= 0 && crypters[type])
		{
			if (data[0] == 0x00)
				crypters[type]->Encrypt(pid, encbuf+2, len-2);
			else
				crypters[type]->Encrypt(pid, encbuf+1, len-1);
		}

		sendto(mysock, encbuf, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));
	}
	global_stats.pktssent++;
}


void BufferPacket(int pid, byte *data, int len, int rel)
{
	Buffer *buf;

	if (clients[pid].flags & NET_FAKE) return;

	/* very special case: unreliable, immediate gets no copying */
	if (rel == NET_IMMEDIATE)
	{
		SendRaw(pid, data, len);
		return;
	}

	buf = GetBuffer();

	buf->pid = pid;
	buf->lastretry = 0;

	/* get data into packet */
	if (rel & NET_RELIABLE)
	{
		buf->d.rel.t1 = 0x00;
		buf->d.rel.t2 = 0x03;
		/* seqnum is set below */
		memcpy(buf->d.rel.data, data, len);
		buf->len = len + 6;
		buf->retries = config.retries;
	}
	else
	{
		memcpy(buf->d.raw, data, len);
		buf->len = len;
		buf->retries = 1;
	}

	pd->LockPlayer(pid);

	/* fill in seqnum */
	if (rel & NET_RELIABLE)
		buf->d.rel.seqnum = clients[pid].s2cn++;

	/* add it to out list */
	DQAdd(&clients[pid].outlist, (DQNode*)buf);

	pd->UnlockPlayer(pid);
	
	/* if it's immediate, do one retry now */
	if (rel & NET_IMMEDIATE)
	{
		SendRaw(pid, buf->d.raw, buf->len);
		buf->lastretry = GTC();
		buf->retries--;
	}
}


void SendToOne(int pid, byte *data, int len, int reliable)
{
	/* see if we can do it the quick way */
	if (len < MAXPACKET && !(reliable & NET_PRESIZE))
		BufferPacket(pid, data, len, reliable);
	else
	{
		int set[2];
		set[0] = pid; set[1] = -1;
		SendToSet(set, data, len, reliable);
	}
}


void SendToArena(int arena, int except, byte *data, int len, int reliable)
{
	int set[MAXPLAYERS+1], i, p = 0;
	if (arena < 0) return;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING && players[i].arena == arena && i != except)
			set[p++] = i;
	pd->UnlockStatus();
	set[p] = -1;
	SendToSet(set, data, len, reliable);
}


void SendToAll(byte *data, int len, int reliable)
{
	int set[MAXPLAYERS+1], i, p = 0;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING)
			set[p++] = i;
	pd->UnlockStatus();
	set[p] = -1;
	SendToSet(set, data, len, reliable);
}


void SendToSet(int *set, byte *data, int len, int rel)
{
	if (len > MAXPACKET || (rel & NET_PRESIZE))
	{	/* too big to send or buffer */
		if (rel & NET_PRESIZE)
		{	/* use 00 0A packets (file transfer) */
			byte _buf[486], *dp = data;
			struct ReliablePacket *pk = (struct ReliablePacket *)_buf;

			pk->t1 = 0x00; pk->t2 = 0x0A;
			pk->seqnum = len;
			while (len > 480)
			{
				memcpy(pk->data, dp, 480);
				SendToSet(set, (byte*)pk, 486, NET_RELIABLE);
				dp += 480;
				len -= 480;
			}
			memcpy(pk->data, dp, len);
			SendToSet(set, (byte*)pk, len+6, NET_RELIABLE);
		}
		else
		{	/* use 00 08/9 packets */
			byte buf[482], *dp = data;

			buf[0] = 0x00; buf[1] = 0x08;
			while (len > 480)
			{
				memcpy(buf+2, dp, 480);
				SendToSet(set, buf, 482, rel);
				dp += 480;
				len -= 480;
			}
			buf[1] = 0x09;
			memcpy(buf+2, dp, len);
			SendToSet(set, buf, len+2, rel);
		}
	}
	else
	{
		while (*set != -1)
		{
			BufferPacket(*set, data, len, rel);
			set++;
		}
	}
}


