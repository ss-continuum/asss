
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

/* DEFINES */

/* #define DUMP_RAW_PACKETS */

#define MAXTYPES 128

/* size of ip/port hash table */
#define HASHSIZE 256

/* resolution for bandwidth limiting, in ticks. this might need tuning. */
#define BANDWIDTH_RES 100

/* ip/udp overhead, in bytes per physical packet */
#define IP_UDP_OVERHEAD 28

/* bits in ClientData.flags */
#define NET_FAKE 0x01
#define NET_INPRESIZE 0x02
#define NET_INBIGPKT 0x04

/* check if a buffer is reliable */
#define IS_REL(buf) ((buf)->d.rel.t1 == 0x00 && (buf)->d.rel.t2 == 0x03)
/* check if a buffer is a connection init packet */

#define IS_CONNINIT(buf)                           \
(                                                  \
	(buf)->d.rel.t1 == 0x00 &&                     \
	(                                              \
	 (buf)->d.rel.t2 == 0x01 ||                    \
	 (buf)->d.rel.t2 == 0x11                       \
	 /* add more packet types that encryption      \
	  * module need to know about here */          \
	)                                              \
)


/* STRUCTS */

#include "packets/reliable.h"

#include "packets/timesync.h"

typedef struct ClientData
{
	/* general flags, hash bucket */
	int flags, nextinbucket;
	/* sequence numbers for reliable packets */
	int s2cn, c2sn;
	/* the address to send packets to */
	struct sockaddr_in sin;
	/* time of last packet recvd and of initial connection */
	unsigned int lastpkt, connecttime;
	/* total amounts sent and recvd */
	unsigned int pktsent, pktrecvd;
	unsigned int bytesent, byterecvd;
	/* encryption type */
	Iencrypt *enc;
	/* big packet buffer pointer and size */
	int bigpktsize, bigpktroom;
	byte *bigpktbuf;
	/* bandwidth control */
	unsigned int sincetime, bytessince, limit;
	/* the outlist */
	DQNode outlist;
} ClientData;



typedef struct Buffer
{
	DQNode node;
	/* pid, len: valid for all buffers */
	int pid, len, pri;
	/* lastretry: only valid for buffers in outlist */
	unsigned int lastretry;
	/* used for reliable buffers in the outlist only { */
	RelCallback callback;
	void *clos;
	/* } */
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
local void SendWithCallback(int *pidset, byte *data, int length,
		RelCallback callback, void *clos);
local void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len);
local void ProcessPacket(int, byte *, int);
local void AddPacket(byte, PacketFunc);
local void RemovePacket(byte, PacketFunc);
local int NewConnection(int type, struct sockaddr_in *, Iencrypt *enc);
local void SetLimit(int pid, int limit);
local void GetStats(struct net_stats *stats);
local void GetClientStats(int pid, struct client_stats *stats);

/* internal: */
local inline int HashIP(struct sockaddr_in);
local inline int LookupIP(struct sockaddr_in);
local inline void SendRaw(int, byte *, int);
local void KillConnection(int pid);
local void ProcessBuffer(Buffer *);
local void InitSockets(void);
local Buffer * GetBuffer(void);
local void BufferPacket(int pid, byte *data, int len, int flags,
		RelCallback callback, void *clos);
local void FreeBuffer(Buffer *);

/* threads: */
local void * RecvThread(void *);
local void * SendThread(void *);
local void * RelThread(void *);

/* network layer header handling: */
local void ProcessKeyResponse(Buffer *);
local void ProcessReliable(Buffer *);
local void ProcessGrouped(Buffer *);
local void ProcessAck(Buffer *);
local void ProcessSyncRequest(Buffer *);
local void ProcessBigData(Buffer *);
local void ProcessPresize(Buffer *);
local void ProcessDrop(Buffer *);
local void ProcessCancel(Buffer *);




/* GLOBAL DATA */

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Iconfig *cfg;

local PlayerData *players;

local LinkedList handlers[MAXTYPES];
local int mysock, myothersock, mybillingsock;

local DQNode freelist, rellist;
local Mutex freemtx, relmtx;
local Condition relcond;
volatile int killallthreads = 0;

/* global clients struct! */
local ClientData clients[MAXPLAYERS+EXTRA_PID_COUNT];
local int clienthash[HASHSIZE];
local Mutex hashmtx;
local Mutex outlistmtx[MAXPLAYERS+EXTRA_PID_COUNT];

/* these are percentages of the total bandwidth that can be used for
 * data of the given priority. this might need tuning. */
local int pri_limits[8] =
{
	/* (slot unused)  */ 0,
	/* NET_PRI_N1   = */ 65,
	/* NET_PRI_ZERO = */ 70,
	/* NET_PRI_P1   = */ 75,
	/* NET_PRI_P2   = */ 80,
	/* NET_PRI_P3   = */ 85,
	/* NET_PRI_P4   = */ 95,
	/* NET_PRI_P5   = */ 100
};

local struct
{
	int port, timeout, selectusec, process;
	int usebilling, droptimeout, billping;
	int encmode, bufferdelta;
	int deflimit;
} config;

local volatile struct net_stats global_stats;

local void (*oohandlers[])(Buffer*) =
{
	NULL, /* 00 - nothing */
	NULL, /* 01 - key initiation */
	ProcessKeyResponse, /* 02 - key response (to be used for billing server) */
	ProcessReliable, /* 03 - reliable */
	ProcessAck, /* 04 - reliable response */
	ProcessSyncRequest, /* 05 - time sync request */
	NULL, /* 06 - time sync response (possible anti-spoof) */
	ProcessDrop, /* 07 - close connection */
	ProcessBigData, /* 08 - bigpacket */
	ProcessBigData, /* 09 - bigpacket2 */
	ProcessPresize, /* 0A - presized bigdata */
	ProcessCancel, /* 0B - cancel presized */
	NULL, /* 0C - nothing */
	NULL, /* 0D - nothing */
	ProcessGrouped /* 0E - grouped */
};

local Inet _int =
{
	INTERFACE_HEAD_INIT(I_NET, "net-udp")
	SendToOne, SendToArena, SendToSet, SendToAll, SendWithCallback,
	ReallyRawSend,
	KillConnection, ProcessPacket, AddPacket, RemovePacket,
	NewConnection, SetLimit, GetStats, GetClientStats
};



/* START OF FUNCTIONS */


EXPORT int MM_net(int action, Imodman *mm_, int arena)
{
	int i;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!cfg || !lm) return MM_FAIL;

		players = pd->players;

		for (i = 0; i < MAXTYPES; i++)
			LLInit(handlers + i);

		/* store configuration params */
		config.port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
		config.timeout = cfg->GetInt(GLOBAL, "Net", "ReliableTimeout", 150);
		config.selectusec = cfg->GetInt(GLOBAL, "Net", "SelectUSec", 10000);
		config.process = cfg->GetInt(GLOBAL, "Net", "ProcessGroup", 5);
		config.encmode = cfg->GetInt(GLOBAL, "Net", "EncryptMode", 0);
		config.droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		config.bufferdelta = cfg->GetInt(GLOBAL, "Net", "MaxBufferDelta", 15);
		config.usebilling = cfg->GetInt(GLOBAL, "Billing", "UseBilling", 0);
		config.billping = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);
		config.deflimit = cfg->GetInt(GLOBAL, "Net", "BandwidthLimit", 3500);

		/* init hash and outlists */
		for (i = 0; i < HASHSIZE; i++)
			clienthash[i] = -1;
		for (i = 0; i < MAXPLAYERS + EXTRA_PID_COUNT; i++)
		{
			clients[i].nextinbucket = -1;
			DQInit(&clients[i].outlist);
			InitMutex(outlistmtx + i);
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
		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		/* release these */
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);

		/* clean up */
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

int LookupIP(struct sockaddr_in sin)
{
	int pid, hashbucket = HashIP(sin);
	LockMutex(&hashmtx);
	pid = clienthash[hashbucket];
	while (pid >= 0)
	{
		if (players[pid].status != S_FREE &&
				clients[pid].sin.sin_addr.s_addr == sin.sin_addr.s_addr &&
				clients[pid].sin.sin_port == sin.sin_port)
			break;
		pid = clients[pid].nextinbucket;
	}
	UnlockMutex(&hashmtx);
	return pid;
}


local void ClearOutlist(int pid)
{
	Buffer *buf, *nbuf;
	DQNode *outlist;

	LockMutex(outlistmtx + pid);

	outlist = &clients[pid].outlist;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		if (buf->callback)
		{
			/* this is ugly, but we have to release the outlist mutex
			 * during these callbacks, because the callback might need
			 * to acquire some mutexes of its own, and we want to avoid
			 * deadlock. */
			UnlockMutex(outlistmtx + pid);
			buf->callback(pid, 0, buf->clos);
			LockMutex(outlistmtx + pid);
		}
		nbuf = (Buffer*)buf->node.next;
		DQRemove((DQNode*)buf);
		FreeBuffer(buf);
	}

	UnlockMutex(outlistmtx + pid);
}


local void InitClient(int i, Iencrypt *enc)
{
	/* free any buffers remaining in the outlist. there probably
	 * shouldn't be any. just be sure. */
	ClearOutlist(i);

	/* set up clientdata */
	LockMutex(outlistmtx + i);
	memset(clients + i, 0, sizeof(ClientData));
	clients[i].c2sn = -1;
	clients[i].limit = config.deflimit;
	clients[i].enc = enc;
	DQInit(&clients[i].outlist);
	UnlockMutex(outlistmtx + i);
}

local void InitPlayer(int i, int type)
{
	/* set up playerdata */
	pd->LockPlayer(i);
	pd->LockStatus();
	memset(players + i, 0, sizeof(PlayerData));
	players[i].pktype = S2C_PLAYERENTERING; /* restore type */
	players[i].arena = -1;
	players[i].pid = i;
	players[i].shiptype = SPEC;
	players[i].attachedto = -1;
	players[i].status = S_CONNECTED;
	players[i].type = type;
	pd->UnlockStatus();
	pd->UnlockPlayer(i);
}


Buffer * GetBuffer(void)
{
	DQNode *dq;

	LockMutex(&freemtx);
	global_stats.buffersused++;
	dq = freelist.prev;
	if (dq == &freelist)
	{
		/* no buffers left, alloc one */
		global_stats.buffercount++;
		UnlockMutex(&freemtx);
		dq = amalloc(sizeof(Buffer));
		DQInit(dq);
	}
	else
	{
		/* grab one off free list */
		DQRemove(dq);
		UnlockMutex(&freemtx);
		/* clear it after releasing mtx */
	}
	memset(dq + 1, 0, sizeof(Buffer) - sizeof(DQNode));
	((Buffer*)dq)->callback = NULL;
	return (Buffer *)dq;
}


void FreeBuffer(Buffer *dq)
{
	LockMutex(&freemtx);
	DQAdd(&freelist, (DQNode*)dq);
	global_stats.buffersused--;
	UnlockMutex(&freemtx);
}


void InitSockets(void)
{
	struct sockaddr_in localsin;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		Error(ERROR_GENERAL, "net: WSAStartup");
#endif

	localsin.sin_family = AF_INET;
	memset(localsin.sin_zero,0,sizeof(localsin.sin_zero));
	localsin.sin_addr.s_addr = htonl(INADDR_ANY);
	localsin.sin_port = htons(config.port);

	if ((mysock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
		Error(ERROR_GENERAL, "net: socket");
	if (bind(mysock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_BIND, "net: bind");

	localsin.sin_port = htons(config.port+1);
	if ((myothersock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
		Error(ERROR_GENERAL, "net: socket");
	if (bind(myothersock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_BIND, "net: bind");

	if (config.usebilling)
	{
		/* get socket */
		if ((mybillingsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
			Error(ERROR_GENERAL, "could not allocate billing socket");

		/* set up billing client struct */
		strcpy(players[PID_BILLER].name, "<<Billing Server>>");
		clients[PID_BILLER].c2sn = -1;
		clients[PID_BILLER].sin.sin_family = AF_INET;
		clients[PID_BILLER].sin.sin_addr.s_addr =
			inet_addr(cfg->GetStr(GLOBAL, "Billing", "IP"));
		clients[PID_BILLER].sin.sin_port =
			htons(cfg->GetInt(GLOBAL, "Billing", "Port", 1850));
		clients[PID_BILLER].limit = 
			cfg->GetInt(GLOBAL, "Billing", "Limit", 15000);
		clients[PID_BILLER].enc = NULL;

		localsin.sin_port = htons(0);
		if (bind(mybillingsock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
			Error(ERROR_BIND, "could not bind billing socket");
	}
}


#ifdef DUMP_RAW_PACKETS
local void dump_pk(byte *data, int len)
{
	FILE *f = popen("xxd", "w");
	fwrite(data, len, 1, f);
	pclose(f);
}
#endif


void * RecvThread(void *dummy)
{
	struct sockaddr_in sin;
	struct timeval tv;
	fd_set fds;
	int len, pid, sinsize, maxfd = 5, n;

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

			if (len < 1) goto freebuf;

#ifdef DUMP_RAW_PACKETS
			printf("RECV: %d bytes\n", len);
			dump_pk(buf->d.raw, len);
#endif

			/***** lock status here *****/
			pd->LockStatus();

			/* search for an existing connection */
			pid = LookupIP(sin);

			if (pid == -1)
			{
				pd->UnlockStatus();
				/* this might be a new connection. make sure it's really
				 * a connection init packet */
				if (IS_CONNINIT(buf))
					DO_CBS(CB_CONNINIT, ALLARENAS, ConnectionInitFunc,
							(&sin, buf->d.raw, len));
				else if (len > 1)
					lm->Log(L_DRIVEL, "<net> Recvd data (%02x %02x ; %d bytes) before connection established", buf->d.raw[0], buf->d.raw[1], len);
				else
					lm->Log(L_DRIVEL, "<net> Recvd data (%02x ; %d byte) before connection established", buf->d.raw[0], len);
				goto freebuf;
			}

			/* grab the status */
			status = players[pid].status;

			if (IS_CONNINIT(buf))
			{
				pd->UnlockStatus();
				/* here, we have a connection init, but it's from a
				 * player we've seen before. there are a few scenarios: */
				if (status == S_CONNECTED)
				{
					/* if the player is in S_CONNECTED, it means that
					 * the connection init response got dropped on the
					 * way to the client. we have to resend it. */
					DO_CBS(CB_CONNINIT, ALLARENAS, ConnectionInitFunc,
							(&sin, buf->d.raw, len));
				}
				else
				{
					/* otherwise, he probably just lagged off or his
					 * client crashed. ideally, we'd postpone this
					 * packet, initiate a logout procedure, and then
					 * process it. we can't do that right now, so drop
					 * the packet, initiate the logout, and hope that
					 * the client re-sends it soon. */
					KillConnection(pid);
				}
				goto freebuf;
			}

			/* we shouldn't get packets in this state, but it's harmless
			 * if we do. */
			if (status == S_TIMEWAIT)
				goto freebufstatus;

			if (status <= S_FREE || status > S_TIMEWAIT)
			{
				lm->Log(L_WARN, "<net> [pid=%d] Packet recieved from bad state %d", pid, status);
				goto freebufstatus;
				/* don't set lastpkt time here */
			}

			pd->UnlockStatus();
			/***** unlock status here *****/

			buf->pid = pid;
			clients[pid].lastpkt = GTC();
			clients[pid].byterecvd += len + IP_UDP_OVERHEAD;
			clients[pid].pktrecvd++;
			global_stats.pktrecvd++;

			/* decrypt the packet */
			{
				Iencrypt *enc = clients[pid].enc;
				if (enc)
					len = enc->Decrypt(pid, buf->d.raw, len);
			}

			if (len != 0)
				buf->len = len;
			else /* bad crc, or something */
			{
				lm->Log(L_MALICIOUS, "<net> [pid=%d] Incoming packet failed crc", pid);
				goto freebuf;
			}

#ifdef DUMP_RAW_PACKETS
			printf("RECV: about to process %d bytes:\n", len);
			dump_pk(buf->d.raw, len);
#endif

			/* hand it off to appropriate place */
			ProcessBuffer(buf);

			goto donehere;
freebufstatus:
			/* unlock status here because we locked it up above and
			 * escaped from the section with a goto. */
			pd->UnlockStatus();
freebuf:
			FreeBuffer(buf);
donehere:
			;
		}

		if (FD_ISSET(myothersock, &fds))
		{
			/* data on port + 1 */
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
					if (players[n].status == S_PLAYING &&
					    (clients[n].flags & NET_FAKE) == 0)
						data[0]++;
				pd->UnlockStatus();
				sendto(myothersock, (char*)data, 8, 0,
						(struct sockaddr*)&sin, sinsize);

				global_stats.pcountpings++;
			}
		}

		if (FD_ISSET(mybillingsock, &fds))
		{
			/* data from billing server */
			Buffer *buf;

			buf = GetBuffer();
			sinsize = sizeof(sin);
			n = recvfrom(mybillingsock, buf->d.raw, MAXPACKET, 0,
					(struct sockaddr*)&sin, &sinsize);
			if (memcmp(&sin, &clients[PID_BILLER].sin, sizeof(sin) - sizeof(sin.sin_zero)))
				lm->Log(L_MALICIOUS,
						"<net> Data recieved on billing server socket from incorrect origin: %s:%i",
						inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
			else if (n > 0)
			{
				buf->pid = PID_BILLER;
				buf->len = n;
				clients[PID_BILLER].lastpkt = GTC();
				ProcessBuffer(buf);
			}
		}
	}
	return NULL;
}


void * SendThread(void *dummy)
{
	byte gbuf[MAXPACKET] = { 0x00, 0x0E };
	unsigned int gtc, i;

	while (!killallthreads)
	{
		usleep(5000);

		/* first send outgoing packets */
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
			if ( (players[i].status > S_FREE && players[i].status < S_TIMEWAIT &&
			      (players[i].flags & NET_FAKE) == 0) ||
			     i >= MAXPLAYERS /* billing needs to send before connected */)
			{
				int pcount = 0, bytessince, pri;
				Buffer *buf, *nbuf;
				byte *gptr = gbuf + 2;
				DQNode *outlist;

				LockMutex(outlistmtx + i);

				gtc = GTC();

				/* check if it's time to clear the bytessent */
				if ( (gtc - clients[i].sincetime) >= BANDWIDTH_RES)
				{
					clients[i].sincetime = gtc;
					clients[i].bytessince = 0;
				}

				/* we keep a local copy of bytessince to account for the
				 * sizes of stuff in grouped packets. */
				bytessince = clients[i].bytessince;

				/* iterate through outlist */
				outlist = &clients[i].outlist;

				/* process highest priority first */
				for (pri = 7; pri > 0; pri--)
				{
					int limit = clients[i].limit * pri_limits[pri] / 100;

					for (buf = (Buffer*)outlist->next;
					     (DQNode*)buf != outlist;
					     buf = nbuf)
					{
						nbuf = (Buffer*)buf->node.next;
						if (
								buf->pri == pri &&
								(gtc - buf->lastretry) > config.timeout &&
								(bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
						{
							/* now check size */
							if (buf->len > 240)
							{
								/* too big for grouped, send immediately */
								bytessince += buf->len + IP_UDP_OVERHEAD;
								SendRaw(buf->pid, buf->d.raw, buf->len);
								buf->lastretry = gtc;
								if (! IS_REL(buf))
								{
									DQRemove((DQNode*)buf);
									FreeBuffer(buf);
								}
							}
							else if (((gptr - gbuf) + buf->len) < (MAXPACKET-10))
							{
								/* add to current grouped packet, if there is room */
								*gptr++ = buf->len;
								memcpy(gptr, buf->d.raw, buf->len);
								gptr += buf->len;
								buf->lastretry = gtc;
								bytessince += buf->len + 1;
								pcount++;
								if (! IS_REL(buf))
								{
									DQRemove((DQNode*)buf);
									FreeBuffer(buf);
								}
							}
						}
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

				UnlockMutex(outlistmtx + i);
			}

		/* process lagouts and timewait
		 * do this in another loop so that we only have to lock/unlock
		 * player status once instead of MAXPLAYERS times around the
		 * loop. */
		pd->LockStatus();
		gtc = GTC();
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
		{
			/* this is used for lagouts and also for timewait */
			int diff = (int)gtc - (int)clients[i].lastpkt;

			/* process lagouts */
			if (players[i].status != S_FREE &&
			    players[i].whenloggedin == 0 && /* acts as flag to prevent dups */
			    clients[i].lastpkt != 0 && /* prevent race */
			    diff > config.droptimeout)
			{
				lm->Log(L_DRIVEL,
						"<net> [%s] Player kicked for no data (lagged off)",
						players[i].name);
				/* FIXME: send "you have been disconnected..." msg */
				/* can't hold lock here for deadlock-related reasons */
				pd->UnlockStatus();
				KillConnection(i);
				pd->LockStatus();
			}

			/* process timewait state */
			/* btw, status is locked in here */
			if (players[i].status == S_TIMEWAIT)
			{
				char drop[2] = {0x00, 0x07};
				int bucket;

				/* here, send disconnection packet */
				SendToOne(i, drop, 2, NET_PRI_P5);

				/* tell encryption to forget about him */
				if (clients[i].enc)
				{
					clients[i].enc->Void(i);
					clients[i].enc = NULL;
				}

				/* log message */
				lm->Log(L_INFO, "<net> [%s] [pid=%d] Disconnected",
						players[i].name, i);

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
					{
						lm->Log(L_ERROR, "<net> Internal error: "
								"established connection not in hash table");
					}
				}

				players[i].status = S_FREE;

				UnlockMutex(&hashmtx);
			}
		}
		pd->UnlockStatus();

	}
	return NULL;
}


void * RelThread(void *dummy)
{
	Buffer *buf, *nbuf;
	int worked = 0;

	LockMutex(&relmtx);
	while (!killallthreads)
	{
		/* wait for reliable pkt to process */
		if (!worked)
			WaitCondition(&relcond, &relmtx);

		worked = 0;
		for (buf = (Buffer*)rellist.next; (DQNode*)buf != &rellist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;

			/* if player is gone, free buffer */
			if (players[buf->pid].status <= S_FREE ||
			    players[buf->pid].status >= S_TIMEWAIT )
			{
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
			}
#if 0
			/* we don't currently use this yet, but it might be useful
			 * in the future. */
			else if (buf->d.rel.t1 != 0x00)
			{
				/* it's a unreliable packet on the rel list. process,
				 * but don't increment sequence number. */
				DQRemove((DQNode*)buf);
				UnlockMutex(&relmtx);

				/* process it */
				ProcessPacket(buf->pid, buf->d.raw, buf->len);

				FreeBuffer(buf);
				LockMutex(&relmtx);
			}
#endif
			else if (buf->d.rel.seqnum == (clients[buf->pid].c2sn + 1) )
			{
				/* else, if seqnum matches, process */
				clients[buf->pid].c2sn++;
				DQRemove((DQNode*)buf);
				/* don't hold mutex while processing packet */
				UnlockMutex(&relmtx);

				/* process it */
				ProcessPacket(buf->pid, buf->d.rel.data, buf->len - 6);

				FreeBuffer(buf);
				LockMutex(&relmtx);
				worked = 1;
			}
			else if (buf->d.rel.seqnum <= clients[buf->pid].c2sn)
			{
				/* this is a duplicated reliable packet */
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
			}
		}
	}
	UnlockMutex(&relmtx);
	return NULL;
}


/* ProcessBuffer
 * unreliable packets will be processed before the call returns and freed.
 * network packets will be processed by the appropriate network handler,
 * which may free it or not.
 */
void ProcessBuffer(Buffer *buf)
{
	if (buf->d.rel.t1 == 0x00)
	{
		if (buf->d.rel.t2 < (sizeof(oohandlers)/sizeof(*oohandlers)) &&
				oohandlers[(int)buf->d.rel.t2])
			(oohandlers[(int)buf->d.rel.t2])(buf);
		else
		{
			lm->Log(L_MALICIOUS, "<net> [%s] [pid=%d] Unknown network subtype %d",
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


int NewConnection(int type, struct sockaddr_in *sin, Iencrypt *enc)
{
	int i = 0, bucket;

	pd->LockStatus();

	if (sin)
	{
		/* try to find this sin in the hash table */
		i = LookupIP(*sin);

		if (i != -1)
		{
			/* we found it. if its status is S_CONNECTED, just return the
			 * pid. it means we have to redo part of the connection init. */
			if (players[i].status == S_CONNECTED)
			{
				pd->UnlockStatus();
				return i;
			}
			else
			{
				/* otherwise, something is horribly wrong. make a note to
				 * this effect. */
				pd->UnlockStatus();
				lm->Log(L_ERROR, "<net> NewConnection called for an established address (pid %d)", i);
				return -1;
			}
		}
	}

	for (i = 0; players[i].status != S_FREE && i < MAXPLAYERS; i++) ;
	pd->UnlockStatus();

	if (i == MAXPLAYERS) return -1;

	if (sin)
		lm->Log(L_DRIVEL,"<net> [pid=%d] New connection from %s:%i",
				i, inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	else
		lm->Log(L_DRIVEL,"<net> [pid=%d] New internal connection", i);

	InitClient(i, enc);

	clients[i].connecttime = GTC();

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

	InitPlayer(i, type);

	return i;
}


void KillConnection(int pid)
{
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
		lm->Log(L_WARN, "<net> Connection to billing server lost");
		/* for normal players, ProcessLoginQueue runs and changes
		 * S_LEAVING_ZONE's to S_TIMEWAIT's after calling callback
		 * functions. but it doesn't do that for biller, so we set
		 * S_TIMEWAIT directly right here. */
		players[pid].status = S_TIMEWAIT;
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

	/* remove outgoing packets from the queue. this partially eliminates
	 * the need for a timewait state. */
	ClearOutlist(pid);
}


void ProcessKeyResponse(Buffer *buf)
{
	if (buf->pid < MAXPLAYERS)
		lm->Log(L_MALICIOUS, "<net> [pid=%d] Key response from client", buf->pid);
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

	if ((sn - clients[buf->pid].c2sn) > config.bufferdelta)
	{
		/* just drop it */
		UnlockMutex(&relmtx);
		lm->Log(L_DRIVEL, "<net> [%s] [pid=%d] Reliable packet with too big delta (%d - %d)",
				players[buf->pid].name, buf->pid,
				sn, clients[buf->pid].c2sn);
		FreeBuffer(buf);
	}
	else
	{
		/* ack and store it */
		struct
		{
			u8 t0, t1;
			i32 seqnum;
		} ack = { 0x00, 0x04, buf->d.rel.seqnum };

		/* add to rel list to be processed */
		DQAdd(&rellist, (DQNode*)buf);
		SignalCondition(&relcond, 0);
		UnlockMutex(&relmtx);

		/* send the ack. use priority 3 so it gets sent as soon as
		 * possible, but not urgent, because we want to combine multiple
		 * ones into packets. */
		BufferPacket(buf->pid, (byte*)&ack, sizeof(ack),
				NET_UNRELIABLE | NET_PRI_P3, NULL, NULL);
	}
}


void ProcessGrouped(Buffer *buf)
{
	int pos = 2, len = 1;

	while (pos < buf->len && len > 0)
	{
		len = buf->d.raw[pos++];
		if (pos + len <= buf->len)
			ProcessPacket(buf->pid, buf->d.raw + pos, len);
		pos += len;
	}
	FreeBuffer(buf);
}


void ProcessAck(Buffer *buf)
{
	Buffer *b, *nbuf;
	DQNode *outlist;

	RelCallback callback = NULL;
	void *clos;

	LockMutex(outlistmtx + buf->pid);
	outlist = &clients[buf->pid].outlist;
	for (b = (Buffer*)outlist->next; (DQNode*)b != outlist; b = nbuf)
	{
		nbuf = (Buffer*)b->node.next;
		if (IS_REL(b) &&
		    b->d.rel.seqnum == buf->d.rel.seqnum)
		{
			callback = b->callback;
			clos = b->clos;
			DQRemove((DQNode*)b);
			FreeBuffer(b);
		}
	}
	UnlockMutex(outlistmtx + buf->pid);

	if (callback)
		callback(buf->pid, 1, clos);

	FreeBuffer(buf);
}


void ProcessSyncRequest(Buffer *buf)
{
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)(buf->d.raw);
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time, GTC() };
	LockMutex(outlistmtx + buf->pid);
	/* note: this bypasses bandwidth limits */
	SendRaw(buf->pid, (byte*)&ts, sizeof(ts));
	UnlockMutex(outlistmtx + buf->pid);
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
		lm->Log(L_MALICIOUS, "<net> [%s] Recieved bigpacket while handling presized data",
				players[pid].name);
		goto reallyexit;
	}

	clients[pid].flags |= NET_INBIGPKT;

	if (newsize > MAXBIGPACKET)
	{
		lm->Log(L_MALICIOUS,
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
			lm->Log(L_ERROR,"<net> [%s] Cannot allocate %d bytes for bigpacket",
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
		lm->Log(L_MALICIOUS,"<net> [%s] Recieved presized data while handling bigpacket", players[pid].name);
		goto reallyexit;
	}

	if (clients[pid].bigpktbuf)
	{
		/* copy data */
		if (size != clients[pid].bigpktroom)
		{
			lm->Log(L_MALICIOUS, "<net> [%s] Presized data length mismatch", players[pid].name);
			goto freepacket;
		}
		memcpy(clients[pid].bigpktbuf+clients[pid].bigpktsize, buf->d.rel.data, buf->len - 6);
		clients[pid].bigpktsize += (buf->len - 6);
	}
	else
	{
		/* allocate it */
		if (size > MAXBIGPACKET)
		{
			lm->Log(L_MALICIOUS,
				"<net> [%s] Big packet: refusing to allocate more than %d bytes",
				players[pid].name,
				MAXBIGPACKET);
		}
		else
		{
			/* copy initial segment */
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


void ProcessCancel(Buffer *req)
{
	/* the client has request a cancel for the file transfer. that means
	 * we have to go through the outlist and remove all packets like
	 * this:
	 * 00 03 xx xx xx xx 00 0A ...
	 * but only ones we haven't sent yet. we then have to reset the next
	 * sequence number to the smallest sequence number of the packets we
	 * just removed. */
	int pid = req->pid;
	Buffer *buf, *nbuf;
	DQNode *outlist;

	FreeBuffer(req);

#if 0
	/* the code will look mostly like this, when it's working */
	LockMutex(outlistmtx + pid);

	outlist = &clients[pid].outlist;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		/* callbacks aren't allowed for file transfers, so don't worry
		 * about them */
		nbuf = (Buffer*)buf->node.next;
		if (buf->d.raw[7] == 0x0A && buf->d.raw[6] == 0x00 &&
		    buf->d.raw[1] == 0x03 && buf->d.raw[0] == 0x00 &&
		    /* buf hasn't been sent (FIXME) */
		   )
		{
			DQRemove((DQNode*)buf);
			FreeBuffer(buf);
		}
	}
	/* FIXME: set sequence number */
	UnlockMutex(outlistmtx + pid);
#endif
}


void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len)
{
	sendto(mysock, pkt, len, 0,
			(struct sockaddr*)sin, sizeof(struct sockaddr_in));
}


/* IMPORTANT: anyone calling SendRaw MUST hold the outlistmtx for the
 * player that they're sending data to if you want bytessince to be
 * accurate. */
void SendRaw(int pid, byte *data, int len)
{
	byte encbuf[MAXPACKET];
	Iencrypt *enc = clients[pid].enc;

	if (clients[pid].flags & NET_FAKE) return;

	if (pid != PID_BILLER)
	{
		memcpy(encbuf, data, len);

#ifdef DUMP_RAW_PACKETS
		printf("SEND: %d bytes to pid %d\n", len, pid);
		dump_pk(encbuf, len);
#endif

		if (enc)
			len = enc->Encrypt(pid, encbuf, len);

#ifdef DUMP_RAW_PACKETS
		printf("SEND: %d bytes (after encryption):\n", len);
		dump_pk(encbuf, len);
#endif

		sendto(mysock, encbuf, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));

	}
	else
	{
		sendto(mybillingsock, data, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));
	}

	clients[pid].bytessince += len + IP_UDP_OVERHEAD;
	clients[pid].bytesent += len + IP_UDP_OVERHEAD;
	clients[pid].pktsent++;
	global_stats.pktsent++;
}


void BufferPacket(int pid, byte *data, int len, int flags,
		RelCallback callback, void *clos)
{
	Buffer *buf;
	int limit;

	if (clients[pid].flags & NET_FAKE) return;

	assert(len < MAXPACKET);

	/* handle default priority */
	if (GET_PRI(flags) == 0) flags |= NET_PRI_DEFAULT;
	limit = clients[pid].limit * pri_limits[GET_PRI(flags)] / 100;

	LockMutex(outlistmtx + pid);

	/* try the fast path */
	if (flags == NET_PRI_P4 || flags == NET_PRI_P5)
		if (clients[pid].bytessince + len + IP_UDP_OVERHEAD <= limit)
		{
			SendRaw(pid, data, len);
			UnlockMutex(outlistmtx + pid);
			return;
		}

	buf = GetBuffer();

	buf->pid = pid;
	buf->lastretry = 0;
	buf->callback = callback;
	buf->clos = clos;
	buf->pri = GET_PRI(flags);
	global_stats.pri_stats[buf->pri]++;

	/* get data into packet */
	if (flags & NET_RELIABLE)
	{
		buf->d.rel.t1 = 0x00;
		buf->d.rel.t2 = 0x03;
		memcpy(buf->d.rel.data, data, len);
		buf->len = len + 6;
		buf->d.rel.seqnum = clients[pid].s2cn++;
	}
	else
	{
		memcpy(buf->d.raw, data, len);
		buf->len = len;
	}

	/* add it to out list */
	DQAdd(&clients[pid].outlist, (DQNode*)buf);

	/* if it's urgent, do one retry now */
	if (GET_PRI(flags) > 5)
		if (clients[pid].bytessince + len + 6 + IP_UDP_OVERHEAD <= limit)
		{
			SendRaw(pid, buf->d.raw, buf->len);
			buf->lastretry = GTC();
		}

	UnlockMutex(outlistmtx + pid);
}


void SendToOne(int pid, byte *data, int len, int flags)
{
	/* see if we can do it the quick way */
	if (len < MAXPACKET && !(flags & NET_PRESIZE))
		BufferPacket(pid, data, len, flags, NULL, NULL);
	else
	{
		int set[2];
		set[0] = pid; set[1] = -1;
		SendToSet(set, data, len, flags);
	}
}


void SendToArena(int arena, int except, byte *data, int len, int flags)
{
	int set[MAXPLAYERS+1], i, p = 0;
	if (arena < 0) return;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING && players[i].arena == arena && i != except)
			set[p++] = i;
	pd->UnlockStatus();
	set[p] = -1;
	SendToSet(set, data, len, flags);
}


void SendToAll(byte *data, int len, int flags)
{
	int set[MAXPLAYERS+1], i, p = 0;
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_PLAYING)
			set[p++] = i;
	pd->UnlockStatus();
	set[p] = -1;
	SendToSet(set, data, len, flags);
}


void SendToSet(int *set, byte *data, int len, int flags)
{
	if (len > MAXPACKET || (flags & NET_PRESIZE))
	{
		/* too big to send or buffer */
		if (flags & NET_PRESIZE)
		{
			/* use 00 0A packets (file transfer) */
			byte _buf[486], *dp = data;
			struct ReliablePacket *pk = (struct ReliablePacket *)_buf;

			pk->t1 = 0x00; pk->t2 = 0x0A;
			pk->seqnum = len;
			while (len > 480)
			{
				memcpy(pk->data, dp, 480);
				/* file transfers go at lowest priority */
				SendToSet(set, (byte*)pk, 486, NET_RELIABLE | NET_PRI_N1);
				dp += 480;
				len -= 480;
			}
			memcpy(pk->data, dp, len);
			SendToSet(set, (byte*)pk, len+6, NET_RELIABLE | NET_PRI_N1);
		}
		else
		{
			/* use 00 08/9 packets */
			byte buf[482], *dp = data;

			buf[0] = 0x00; buf[1] = 0x08;
			while (len > 480)
			{
				memcpy(buf+2, dp, 480);
				SendToSet(set, buf, 482, flags);
				dp += 480;
				len -= 480;
			}
			buf[1] = 0x09;
			memcpy(buf+2, dp, len);
			SendToSet(set, buf, len+2, flags);
		}
	}
	else
		while (*set != -1)
		{
			BufferPacket(*set, data, len, flags, NULL, NULL);
			set++;
		}
}


void SendWithCallback(
		int *set,
		byte *data,
		int len,
		RelCallback callback,
		void *clos)
{
	/* we can't handle big packets here */
	assert(len < MAXPACKET);

	while (*set != -1)
	{
		BufferPacket(*set, data, len, NET_RELIABLE, callback, clos);
		set++;
	}
}


i32 GetIP(int pid)
{
	return clients[pid].sin.sin_addr.s_addr;
}


void SetLimit(int pid, int limit)
{
	clients[pid].limit = limit * BANDWIDTH_RES / 100;
}


void GetStats(struct net_stats *stats)
{
	if (stats)
		*stats = global_stats;
}

void GetClientStats(int pid, struct client_stats *stats)
{
	ClientData *client = clients + pid;

	if (!stats || PID_BAD(pid)) return;

#define ASSIGN(field) stats->field = client->field
	ASSIGN(s2cn); ASSIGN(c2sn);
	ASSIGN(pktsent); ASSIGN(pktrecvd); ASSIGN(bytesent); ASSIGN(byterecvd);
	ASSIGN(connecttime);
#undef ASSIGN
	/* encryption */
	if (clients[pid].enc)
		stats->encname = clients[pid].enc->head.name;
	else
		stats->encname = "none";
	/* convert to bytes per second */
	stats->limit = clients->limit * 100 / BANDWIDTH_RES;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(clients[pid].sin.sin_addr), 16);
	stats->port = clients[pid].sin.sin_port;
}

