
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
	int s2cn, c2sn, flags;
	int lastack, nextinbucket;
	struct sockaddr_in sin;
	unsigned int lastpkt, key;
	short enctype;
	int bigpktsize, bigpktroom;
	byte *bigpktbuf;
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
local void DropClient(int);
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
local void InitSockets();
local Buffer * GetBuffer();
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
local void ProcessResponse(Buffer *);
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

local DQNode freelist, rellist, outlist;
local Mutex freemtx, relmtx, outmtx;
local Condition outcond, relcond;
volatile int killallthreads = 0;

/* global clients struct!: */
local ClientData clients[MAXPLAYERS+EXTRA_PID_COUNT];
local int clienthash[HASHSIZE];
local Mutex hashmtx;

local struct
{
	int port, retries, timeout, selectusec, process;
	int biglimit, usebilling, droptimeout, billping;
	int encmode;
} config;

local struct
{
	int pcountpings, buffercount;
} global_stats;

local void (*oohandlers[])(Buffer*) =
{
	NULL, /* 00 - nothing */
	ProcessKey, /* 01 - key initiation */
	ProcessKeyResponse, /* 02 - key response (to be used for billing server) */
	ProcessReliable, /* 03 - reliable */
	ProcessResponse, /* 04 - reliable response */
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
	DropClient, ProcessPacket, AddPacket, RemovePacket,
	NewConnection, GetIP
};



/* START OF FUNCTIONS */


int MM_net(int action, Imodman *mm)
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
		config.usebilling = cfg->GetInt(GLOBAL, "Billing", "UseBilling", 0);
		config.billping = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);

		/* init hash */
		for (i = 0; i < HASHSIZE; i++)
			clienthash[i] = -1;
		for (i = 0; i < MAXPLAYERS + EXTRA_PID_COUNT; i++)
			clients[i].nextinbucket = -1;
		InitMutex(&hashmtx);

		/* init buffers */
		InitCondition(&outcond); InitCondition(&relcond);
		InitMutex(&freemtx); InitMutex(&relmtx); InitMutex(&outmtx);
		DQInit(&freelist); DQInit(&rellist); DQInit(&outlist);

		/* get the sockets */
		InitSockets();

		/* start the threads */
		StartThread(RecvThread, NULL);
		StartThread(SendThread, NULL);
		StartThread(RelThread, NULL);

		/* install ourself */
		mm->RegInterface(I_NET, &_int);
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
	}
	return MM_OK;
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


Buffer * GetBuffer()
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
		memset(dq + 1, 0, sizeof(Buffer) - sizeof(DQNode));
	}
	return (Buffer *)dq;
}


void FreeBuffer(Buffer *dq)
{
	LockMutex(&freemtx);
	DQAdd(&freelist, (DQNode*)dq);
	UnlockMutex(&freemtx);
}


void InitSockets()
{
	struct sockaddr_in localsin;

	localsin.sin_family = AF_INET;
	memset(localsin.sin_zero,0,sizeof(localsin.sin_zero));
	localsin.sin_addr.s_addr = htonl(INADDR_ANY);
	localsin.sin_port = htons(config.port);

	if ((mysock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_NORMAL,"net: socket");
	if (bind(mysock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_NORMAL,"net: bind");

	localsin.sin_port = htons(config.port+1);
	if ((myothersock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_NORMAL,"net: socket");
	if (bind(myothersock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_NORMAL,"net: bind");

	if (config.usebilling)
	{
		/* get socket */
		if ((mybillingsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
			Error(ERROR_NORMAL, "could not allocate billing socket");

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

			
			/* log->Log(LOG_DEBUG,"Got %i bytes from %s:%i", len, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port)); */
			/*
			log->Log(LOG_DEBUG,"recv: %2x %2x %2x %2x %2x %2x %2x %2x",
					buf->d.raw[0],
					buf->d.raw[1],
					buf->d.raw[2],
					buf->d.raw[3],
					buf->d.raw[4],
					buf->d.raw[5],
					buf->d.raw[6],
					buf->d.raw[7]
					);
			*/

			buf->len = len;

			/* search for an existing connection */
			hashbucket = HashIP(sin);
			/* LOCK: lock status here? */
			LockMutex(&hashmtx);
			pid = clienthash[hashbucket];
			while (pid >= 0)
			{
				if (    PLAYER_IS_CONNECTED(pid)
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
					log->Log(LOG_IMPORTANT,"Too many players! Dropping extra connections!");
					goto donehere;
				}
				else
				{
					log->Log(LOG_USELESSINFO,"New connection (%s:%i) assigning pid %i",
							inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), pid);
				}
			}

			/* log->Log(LOG_DEBUG,"    --> has status %d", players[pid].status); */

			buf->pid = pid;
			/* set the last packet time */
			clients[pid].lastpkt = GTC();

			/* decrypt the packet */
			type = clients[pid].enctype;
			if (type >= 0 && type < MAXENCRYPT && crypters[type])
			{
				/* log->Log(LOG_DEBUG,"calling decrypt: %X %X", buf->d.rel.t1, buf->d.rel.t2); */
				if (buf->d.rel.t1 == 0x00)
					crypters[type]->Decrypt(pid, buf->d.raw+2, len-2);
				else
					crypters[type]->Decrypt(pid, buf->d.raw+1, len-1);
			}

			/* hand it off to appropriate place */
			ProcessBuffer(buf);

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
			/*log->Log(LOG_DEBUG, "%i bytes from billing server", n); */
			if (memcmp(&sin, &clients[PID_BILLER].sin, sinsize))
				log->Log(LOG_BADDATA,
						"Data recieved on billing server socket from incorrect origin: %s:%i",
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
#define MAXGROUPED 20
	static unsigned pcount[MAXPLAYERS+EXTRA_PID_COUNT], bigcount[MAXPLAYERS+EXTRA_PID_COUNT];
	static Buffer *order[MAXPLAYERS+EXTRA_PID_COUNT][MAXGROUPED];
	Buffer *buf, *nbuf, *it;
	unsigned int gtc = GTC(), i, bucket;

	while (!killallthreads)
	{
		/* zero some arrays */
		memset(pcount, 0, (MAXPLAYERS+EXTRA_PID_COUNT) * sizeof(unsigned));
		memset(bigcount, 0, (MAXPLAYERS+EXTRA_PID_COUNT) * sizeof(unsigned));

		/* grab first buffer */
		LockMutex(&outmtx);
		WaitConditionTimed(&outcond, &outmtx, 200);
		buf = (Buffer*)outlist.next;

		for (buf = (Buffer*)outlist.next; (DQNode*)buf != &outlist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;

			/* remove packets that client has acked */
			if (    buf->d.rel.t1 == 0x00
			     && buf->d.rel.t2 == 0x03
			     && clients[buf->pid].lastack >= buf->d.rel.seqnum)
				buf->retries = 0;

			/* LOCK: lock status here? */
			/* check if the player still exists */
			if (!PLAYER_IS_CONNECTED(buf->pid))
				buf->retries = 0;

			/* check if we can send it now */
			if (    buf->retries > 0
			     && (gtc - buf->lastretry) > config.timeout)
			{
				if (buf->len > 240)
				{	/* too big for grouped, send immediately */
					if (bigcount[buf->pid]++ < config.biglimit)
					{
						buf->lastretry = GTC();
						buf->retries--;
						/* UnlockMutex(&outmtx); // skipped to save speed */
						SendRaw(buf->pid, buf->d.raw, buf->len);
						/* LockMutex(&outmtx); */
					}
				}
				else if (pcount[buf->pid] < MAXGROUPED)
					order[buf->pid][pcount[buf->pid]++] = buf;
					/* packets over the group limit don't get sent */
					/* at all until the first bunch are ack'd */
			}

			/* free buffers if possible */
			if (buf->retries < 1)
			{	/* release buffer */
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
			}
		}
		UnlockMutex(&outmtx);

		/* send packets! */
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
		{
			if (pcount[i] == 1)
			{	/* only one packet for this person, send it */
				it = order[i][0];
				it->lastretry = gtc;
				it->retries--;
				SendRaw(i, it->d.raw, it->len);
			}
			else if (pcount[i] > 1)
			{	/* group all packets to send */
				static byte gbuf[MAXPACKET];
				byte *current = gbuf + 2;
				int j;

				gbuf[0] = 0x00; gbuf[1] = 0x0E;

				for (j = 0; j < pcount[i]; j++)
				{
					it = order[i][j];
					if ( (current-gbuf+it->len+5) < MAXPACKET)
					{
						*current++ = it->len;
						memcpy(current, it->d.raw, it->len);
						current += it->len;
						it->lastretry = gtc;
						it->retries--;
					}
					else
						break;
				}
				/* send it, finally */
				SendRaw(i, gbuf, current - gbuf);
			}
		}

		/* process lagouts and timewait */
		gtc = GTC();
		pd->LockStatus();
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
		{
			/* process lagouts */
			if (   players[i].status != S_FREE
			    && players[i].whenloggedin == 0 /* acts as flag to prevent dups */
			    && (gtc - clients[i].lastpkt) > config.droptimeout)
			{
				byte pk7[] = { 0x00, 0x07 };
				log->Log(LOG_USELESSINFO, "Lag out: %s", players[i].name);
				/* FIXME: send "you have been disconnected..." msg */
				SendRaw(i, pk7, 2);
				/* can't hold lock here for deadlock-related reasons */
				pd->UnlockStatus();
				KillConnection(i);
				pd->LockStatus();
			}

			/* process timewait states
			 * this is done with two states to ensure a complete pass
			 * through the outgoing buffer before marking these pids as
			 * free */
			if (players[i].status == S_TIMEWAIT2)
			{
				/* remove from hash table. we do this now so that
				 * packets that arrive after the connection is closed
				 * (because of udp misorderings) don't cause a new
				 * connection to be created (at least for a short while)
				 */
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
						log->Log(LOG_BADDATA, "net: Internal error: established connection not in hash table");
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

		/* give up some time */
		sched_yield();
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
			if (   players[buf->pid].status == S_FREE
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

				/* log->Log(LOG_DEBUG, "processing rel pkt, %i bytes, type %i (((", buf->len, buf->d.rel.data[0]); */

				/* process it */
				ProcessPacket(buf->pid, buf->d.rel.data, buf->len - 6);

				/* log->Log(LOG_DEBUG, "done processing rel pkt )))"); */

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
			log->Log(LOG_BADDATA, "Unknown network subtype %i from '%s'",
					buf->d.rel.t2, players[buf->pid].name);
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

	/* set up clientdata */
	memset(clients + i, 0, sizeof(ClientData));
	clients[i].c2sn = -1;
	clients[i].enctype = -1;
	clients[i].lastack = -1;

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
	pd->LockPlayer(i);
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
	 * likely), just generate one and process it. this must set status
	 * to S_LEAVING_ARENA */
	if (players[pid].arena >= 0)
		ProcessPacket(pid, &leaving, 1);

	pd->LockStatus();

	/* set status */
	if (pid == PID_BILLER)
	{
		log->Log(LOG_IMPORTANT, "Connection to billing server lost");
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
	log->Log(LOG_INFO, "Player '%s' disconnected", players[pid].name);
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
		log->Log(LOG_BADDATA, "net: pid %d initiated key exchange from incorrect stage: %d, dropping", buf->pid, player->status);
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
		log->Log(LOG_BADDATA, "Unknown encryption type attempted to connect");

	FreeBuffer(buf);
}


void ProcessKeyResponse(Buffer *buf)
{
	if (buf->pid < MAXPLAYERS)
		log->Log(LOG_BADDATA, "Key response from normal client!");
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
	/* FIXME: just drop packet if seqnum is too far ahead
	 * (prevent DoS by forcing server to buffer lots of packets) */

	/* ack */
	buf->d.rel.t2++;
	SendRaw(buf->pid, buf->d.raw, 6);
	buf->d.rel.t2--;

	/* add to rel list to be processed */
	LockMutex(&relmtx);
	DQAdd(&rellist, (DQNode*)buf);
	UnlockMutex(&relmtx);
	SignalCondition(&relcond, 0);
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


void ProcessResponse(Buffer *buf)
{
	if (buf->d.rel.seqnum > clients[buf->pid].lastack)
		clients[buf->pid].lastack = buf->d.rel.seqnum;
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
		log->Log(LOG_BADDATA, "Recieved bigpacket while handling presized data! (%s)",
				players[pid].name);
		goto reallyexit;
	}

	clients[pid].flags |= NET_INBIGPKT;

	if (newsize > MAXBIGPACKET)
	{
		log->Log(LOG_BADDATA,
			"Big packet: refusing to allocate more than %i bytes for '%s'",
			MAXBIGPACKET, players[pid].name);
		goto freebigbuf;
	}

	if (clients[pid].bigpktroom < newsize)
	{
		clients[pid].bigpktroom *= 2;
		if (clients[pid].bigpktroom < newsize) clients[pid].bigpktroom = newsize;
		newbuf = realloc(clients[pid].bigpktbuf, clients[pid].bigpktroom); 
		if (!newbuf)
		{
			log->Log(LOG_ERROR,"Cannot allocate %i for bigpacket (%s)",
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
		log->Log(LOG_BADDATA,"Recieved presized data while handling bigpacket! (%s)",
				players[pid].name);
		goto reallyexit;
	}

	if (clients[pid].bigpktbuf)
	{	/* copy data */
		if (size != clients[pid].bigpktroom)
		{
			log->Log(LOG_BADDATA, "Presized data length mismatch! (%s)",
					players[pid].name);
			goto freepacket;
		}
		memcpy(clients[pid].bigpktbuf+clients[pid].bigpktsize, buf->d.rel.data, buf->len - 6);
		clients[pid].bigpktsize += (buf->len - 6);
	}
	else
	{	/* allocate it */
		if (size > MAXBIGPACKET)
		{
			log->Log(LOG_BADDATA,
				"Big packet: refusing to allocate more than %i bytes (%s)",
				MAXBIGPACKET, players[pid].name);
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


void DropClient(int pid)
{
	byte pkt1[2] = {0x00, 0x07};

	if (PLAYER_IS_CONNECTED(pid))
	{
		SendRaw(pid, pkt1, 2);
		KillConnection(pid);
	}
}


void SendRaw(int pid, byte *data, int len)
{
	static byte encbuf[MAXPACKET];
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

	if (rel & NET_RELIABLE)
	{
		buf->d.rel.t1 = 0x00;
		buf->d.rel.t2 = 0x03;
		buf->d.rel.seqnum = clients[pid].s2cn++;
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

	if (rel & NET_IMMEDIATE)
	{
		SendRaw(pid, buf->d.raw, buf->len);
		buf->lastretry = GTC();
		buf->retries--;
	}

	/* add it to out list */
	LockMutex(&outmtx);
	DQAdd(&outlist, (DQNode*)buf);
	UnlockMutex(&outmtx);
	SignalCondition(&outcond, 0);
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


