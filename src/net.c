
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

/* defines */

#define MAXTYPES 64

/* ip/udp overhead, in bytes per physical packet */
#define IP_UDP_OVERHEAD 28

/* packets to queue up for sending files */
#define QUEUE_PACKETS 30
/* threshold to start queuing up more packets */
#define QUEUE_THRESHOLD 10

/* check whether we manage this client */
#define IS_OURS(pid) (players[(pid)].type == T_CONT || players[(pid)].type == T_VIE)

/* check if a buffer is reliable */
#define IS_REL(buf) ((buf)->d.rel.t2 == 0x03 && (buf)->d.rel.t1 == 0x00)

/* check if a buffer is presized */
#define IS_PRESIZED(buf) ((buf)->d.rel.t2 == 0x0A && (buf)->d.rel.t1 == 0x00)

/* check if a buffer is a connection init packet */
#define IS_CONNINIT(buf)                        \
(                                               \
 (buf)->d.rel.t1 == 0x00 &&                     \
 (                                              \
  (buf)->d.rel.t2 == 0x01 ||                    \
  (buf)->d.rel.t2 == 0x11                       \
  /* add more packet types that encryption      \
   * module need to know about here */          \
 )                                              \
)


/* structs */

#include "packets/reliable.h"

#include "packets/timesync.h"

struct sized_send_data
{
	void (*request_data)(void *clos, int offset, byte *buf, int needed);
	void *clos;
	int totallen, offset;
};

typedef struct ClientData
{
	/* the address to send packets to */
	struct sockaddr_in sin;
	/* hash bucket */
	int nextinbucket;
	/* sequence numbers for reliable packets */
	int s2cn, c2sn;
	/* time of last packet recvd and of initial connection */
	unsigned int lastpkt;
	/* total amounts sent and recvd */
	unsigned int pktsent, pktrecvd;
	unsigned int bytesent, byterecvd;
	/* duplicate reliable packets and reliable retries */
	unsigned int reldups, retries;
	/* encryption type */
	Iencrypt *enc;
	/* stuff for recving sized packets, protected by player mtx */
	struct
	{
		byte type;
		int totallen, offset;
	} sizedrecv;
	/* stuff for recving big packets, protected by player mtx */
	struct
	{
		int size, room;
		byte *buf;
	} bigrecv;
	/* stuff for sending sized packets, protected by outlist mtx */
	LinkedList sizedsends;
	/* bandwidth control */
	unsigned int sincetime, bytessince, limit;
	/* the outlist */
	DQNode outlist;
} ClientData;



typedef struct Buffer
{
	DQNode node;
	/* pid, len: valid for all buffers */
	int pid, len;
	unsigned char pri, reliable, unused1, unused2;
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


/* prototypes */

/* interface: */
local void SendToOne(int, byte *, int, int);
local void SendToArena(int, int, byte *, int, int);
local void SendToSet(int *, byte *, int, int);
local void SendToTarget(const Target *, byte *, int, int);
local void SendToAll(byte *, int, int);
local void SendWithCallback(int *pidset, byte *data, int length,
		RelCallback callback, void *clos);
local void SendSized(int pid, void *clos, int len,
		void (*req)(void *clos, int offset, byte *buf, int needed));

local void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len);
local void ProcessPacket(int, byte *, int);
local void AddPacket(int, PacketFunc);
local void RemovePacket(int, PacketFunc);
local void AddSizedPacket(int, SizedPacketFunc);
local void RemoveSizedPacket(int, SizedPacketFunc);
local int NewConnection(int type, struct sockaddr_in *, Iencrypt *enc);
local void SetLimit(int pid, int limit);
local void GetStats(struct net_stats *stats);
local void GetClientStats(int pid, struct net_client_stats *stats);

/* internal: */
local inline int HashIP(struct sockaddr_in);
local inline int LookupIP(struct sockaddr_in);
local inline void SendRaw(int, byte *, int);
local void KillConnection(int pid);
local void ProcessBuffer(Buffer *);
local int InitSockets(void);
local Buffer * GetBuffer(void);
local Buffer * BufferPacket(int pid, byte *data, int len, int flags,
		RelCallback callback, void *clos);
local void FreeBuffer(Buffer *);

/* threads: */
local void * RecvThread(void *);
local void * SendThread(void *);
local void * RelThread(void *);

local int QueueMoreData(void *);

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




/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Iconfig *cfg;
local Ilagcollect *lagc;

local PlayerData *players;

local LinkedList handlers[MAXTYPES];
local LinkedList sizedhandlers[MAXTYPES];
local LinkedList billhandlers[MAXTYPES];
local int serversock, pingsock, clientsock;

local DQNode freelist, rellist;
local pthread_mutex_t freemtx, relmtx;
local pthread_cond_t relcond;
volatile int killallthreads = 0;

/* global clients struct! */
local ClientData clients[MAXPLAYERS+EXTRA_PID_COUNT];
local int clienthash[CFG_HASHSIZE];
local pthread_mutex_t hashmtx;
local pthread_mutex_t outlistmtx[MAXPLAYERS+EXTRA_PID_COUNT];

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
	int bufferdelta, deflimit;
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
	SendToOne, SendToArena, SendToSet, SendToTarget,
	SendToAll, SendWithCallback, SendSized,
	ReallyRawSend,
	KillConnection,
	AddPacket, RemovePacket, AddSizedPacket, RemoveSizedPacket,
	NewConnection, SetLimit, GetStats, GetClientStats
};



/* start of functions */


EXPORT int MM_net(int action, Imodman *mm_, int arena)
{
	int i;
	pthread_t thd;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		if (!pd || !cfg || !lm || !ml) return MM_FAIL;

		players = pd->players;

		/* store configuration params */
		config.port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
		config.timeout = cfg->GetInt(GLOBAL, "Net", "ReliableTimeout", 150);
		config.selectusec = cfg->GetInt(GLOBAL, "Net", "SelectUSec", 10000);
		config.process = cfg->GetInt(GLOBAL, "Net", "ProcessGroup", 5);
		config.droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		config.bufferdelta = cfg->GetInt(GLOBAL, "Net", "MaxBufferDelta", 15);
		config.usebilling = cfg->GetInt(GLOBAL, "Billing", "UseBilling", 0);
		config.billping = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);
		config.deflimit = cfg->GetInt(GLOBAL, "Net", "BandwidthLimit", 3500);

		/* get the sockets */
		if (InitSockets())
			return MM_FAIL;

		for (i = 0; i < MAXTYPES; i++)
		{
			LLInit(handlers + i);
			LLInit(sizedhandlers + i);
			LLInit(billhandlers + i);
		}

		/* init hash and outlists */
		for (i = 0; i < CFG_HASHSIZE; i++)
			clienthash[i] = -1;
		for (i = 0; i < MAXPLAYERS + EXTRA_PID_COUNT; i++)
		{
			clients[i].nextinbucket = -1;
			DQInit(&clients[i].outlist);
			pthread_mutex_init(outlistmtx + i, NULL);
		}
		pthread_mutex_init(&hashmtx, NULL);

		/* init buffers */
		pthread_cond_init(&relcond, NULL);
		pthread_mutex_init(&freemtx, NULL);
		pthread_mutex_init(&relmtx, NULL);
		DQInit(&freelist); DQInit(&rellist);

		/* start the threads */
		pthread_create(&thd, NULL, RecvThread, NULL);
		pthread_create(&thd, NULL, SendThread, NULL);
		pthread_create(&thd, NULL, RelThread, NULL);

		ml->SetTimer(QueueMoreData, 200, 150, NULL);

		/* install ourself */
		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(QueueMoreData);

		/* disconnect all clients nicely */
		for (i = 0; i < MAXPLAYERS; i++)
			if (players[i].status > S_FREE &&
			    IS_OURS(i))
			{
				byte discon[2] = { 0x00, 0x07 };
				SendRaw(i, discon, 2);
			}

		/* clean up */
		for (i = 0; i < MAXTYPES; i++)
		{
			LLEmpty(handlers + i);
			LLEmpty(sizedhandlers + i);
			LLEmpty(billhandlers + i);
		}

		/* let threads die */
		killallthreads = 1;
		/* note: we don't join them because they could be blocked on
		 * something, and who ever wants to unload net anyway? */

		close(serversock);
		close(pingsock);
		if (config.usebilling) close(clientsock);

		/* release these */
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(lagc);

		return MM_OK;
	}
	return MM_FAIL;
}


void AddPacket(int t, PacketFunc f)
{
	if (t >= 0 && t < MAXTYPES)
		LLAdd(handlers+t, f);
	else if (t >= PKT_BILLER_OFFSET && t < PKT_BILLER_OFFSET + MAXTYPES)
		LLAdd(billhandlers+(t-PKT_BILLER_OFFSET), f);
}

void RemovePacket(int t, PacketFunc f)
{
	if (t >= 0 && t < MAXTYPES)
		LLRemove(handlers+t, f);
	else if (t >= PKT_BILLER_OFFSET && t < PKT_BILLER_OFFSET + MAXTYPES)
		LLRemove(billhandlers+(t-PKT_BILLER_OFFSET), f);
}

void AddSizedPacket(int t, SizedPacketFunc f)
{
	if (t >= 0 && t < MAXTYPES)
		LLAdd(sizedhandlers+t, f);
}

void RemoveSizedPacket(int t, SizedPacketFunc f)
{
	if (t >= 0 && t < MAXTYPES)
		LLRemove(sizedhandlers+t, f);
}


int HashIP(struct sockaddr_in sin)
{
	register unsigned ip = sin.sin_addr.s_addr;
	register unsigned short port = sin.sin_port;
	return ((port>>1) ^ (ip) ^ (ip>>23) ^ (ip>>17)) & (CFG_HASHSIZE-1);
}

int LookupIP(struct sockaddr_in sin)
{
	int pid, hashbucket = HashIP(sin);
	pthread_mutex_lock(&hashmtx);
	pid = clienthash[hashbucket];
	while (pid >= 0)
	{
		if (players[pid].status != S_FREE &&
				clients[pid].sin.sin_addr.s_addr == sin.sin_addr.s_addr &&
				clients[pid].sin.sin_port == sin.sin_port)
			break;
		pid = clients[pid].nextinbucket;
	}
	pthread_mutex_unlock(&hashmtx);
	return pid;
}


local void ClearOutlist(int pid)
{
	Buffer *buf, *nbuf;
	DQNode *outlist;
	Link *l;

	pthread_mutex_lock(outlistmtx + pid);

	outlist = &clients[pid].outlist;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		nbuf = (Buffer*)buf->node.next;
		if (buf->callback)
		{
			/* this is ugly, but we have to release the outlist mutex
			 * during these callbacks, because the callback might need
			 * to acquire some mutexes of its own, and we want to avoid
			 * deadlock. */
			pthread_mutex_unlock(outlistmtx + pid);
			buf->callback(pid, 0, buf->clos);
			pthread_mutex_lock(outlistmtx + pid);
		}
		DQRemove((DQNode*)buf);
		FreeBuffer(buf);
	}

	for (l = LLGetHead(&clients[pid].sizedsends); l; l = l->next)
	{
		struct sized_send_data *sd = l->data;
		sd->request_data(sd->clos, 0, NULL, 0);
		afree(sd);
	}
	LLEmpty(&clients[pid].sizedsends);

	pthread_mutex_unlock(outlistmtx + pid);
}


local void InitClient(int i, Iencrypt *enc)
{
	/* free any buffers remaining in the outlist. there probably
	 * shouldn't be any. just be sure. */
	ClearOutlist(i);

	/* set up clientdata */
	pthread_mutex_lock(outlistmtx + i);
	memset(clients + i, 0, sizeof(ClientData));
	clients[i].c2sn = -1;
	clients[i].limit = config.deflimit;
	clients[i].enc = enc;
	LLInit(&clients[i].sizedsends);
	DQInit(&clients[i].outlist);
	pthread_mutex_unlock(outlistmtx + i);
}


Buffer * GetBuffer(void)
{
	DQNode *dq;

	pthread_mutex_lock(&freemtx);
	global_stats.buffersused++;
	dq = freelist.prev;
	if (dq == &freelist)
	{
		/* no buffers left, alloc one */
		global_stats.buffercount++;
		pthread_mutex_unlock(&freemtx);
		dq = amalloc(sizeof(Buffer));
		DQInit(dq);
	}
	else
	{
		/* grab one off free list */
		DQRemove(dq);
		pthread_mutex_unlock(&freemtx);
		/* clear it after releasing mtx */
	}
	memset(dq + 1, 0, sizeof(Buffer) - sizeof(DQNode));
	((Buffer*)dq)->callback = NULL;
	return (Buffer *)dq;
}


void FreeBuffer(Buffer *dq)
{
	pthread_mutex_lock(&freemtx);
	DQAdd(&freelist, (DQNode*)dq);
	global_stats.buffersused--;
	pthread_mutex_unlock(&freemtx);
}


int InitSockets(void)
{
	struct sockaddr_in localsin;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		return -1;
#endif

	localsin.sin_family = AF_INET;
	memset(localsin.sin_zero,0,sizeof(localsin.sin_zero));
	localsin.sin_addr.s_addr = htonl(INADDR_ANY);
	localsin.sin_port = htons(config.port);

	if ((serversock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
	{
		perror("socket");
		return -1;
	}
	if (bind(serversock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
	{
		perror("bind");
		return -1;
	}

	localsin.sin_port = htons(config.port+1);
	if ((pingsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
	{
		perror("socket");
		return -1;
	}
	if (bind(pingsock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
	{
		perror("bind");
		return -1;
	}

	if (config.usebilling)
	{
		/* get socket */
		if ((clientsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
		{
			perror("socket");
			return -1;
		}

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
	}
	
	return 0;
}


#ifdef CFG_DUMP_RAW_PACKETS
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
				FD_SET(clientsock, &fds);
				if (clientsock > maxfd) maxfd = clientsock;
			}
			FD_SET(pingsock, &fds); if (pingsock > maxfd) maxfd = pingsock;
			FD_SET(serversock, &fds); if (serversock > maxfd) maxfd = serversock;

			tv.tv_sec = 10;
			tv.tv_usec = 0;

			/* perform select */
		} while (select(maxfd+1, &fds, NULL, NULL, &tv) < 1 && !killallthreads);

		/* first handle the main socket */
		if (FD_ISSET(serversock, &fds))
		{
			int status;
			Buffer *buf;

			buf = GetBuffer();
			sinsize = sizeof(sin);
			len = recvfrom(serversock, buf->d.raw, MAXPACKET, 0,
					(struct sockaddr*)&sin, &sinsize);

			if (len < 1) goto freebuf;

#ifdef CFG_DUMP_RAW_PACKETS
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

#ifdef CFG_DUMP_RAW_PACKETS
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

		if (FD_ISSET(pingsock, &fds))
		{
			/* data on port + 1 */
			unsigned int data[2];

			sinsize = sizeof(sin);
			n = recvfrom(pingsock, (char*)data, 4, 0,
					(struct sockaddr*)&sin, &sinsize);

			if (n == 4)
			{
				data[1] = data[0];
				data[0] = 0;
				pd->LockStatus();
				for (n = 0; n < MAXPLAYERS; n++)
					if (players[n].status == S_PLAYING &&
					    players[n].type != T_FAKE)
						data[0]++;
				pd->UnlockStatus();
				sendto(pingsock, (char*)data, 8, 0,
						(struct sockaddr*)&sin, sinsize);

				global_stats.pcountpings++;
			}
		}

		if (FD_ISSET(clientsock, &fds))
		{
			/* data from billing server */
			Buffer *buf;

			buf = GetBuffer();
			sinsize = sizeof(sin);
			n = recvfrom(clientsock, buf->d.raw, MAXPACKET, 0,
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


local void submit_rel_stats(int pid)
{
	if (lagc)
	{
		struct ReliableLagData rld;
		rld.reldups = clients[pid].reldups;
		/* the plus one is because c2sn is the rel id of the last packet
		 * that we've seen, not the one we want to see. */
		rld.c2sn = clients[pid].c2sn + 1;
		rld.retries = clients[pid].retries;
		rld.s2cn = clients[pid].s2cn;
		lagc->RelStats(pid, &rld);
	}
}


local void end_sized(int pid, int success)
{
	if (clients[pid].sizedrecv.offset != 0)
	{
		Link *l;
		u8 type = clients[pid].sizedrecv.type;
		int arg = success ? clients[pid].sizedrecv.totallen : -1;
		/* tell listeners that they're cancelled */
		if (type < MAXTYPES)
			for (l = LLGetHead(sizedhandlers + type); l; l = l->next)
				((SizedPacketFunc)(l->data))(pid, NULL, 0, arg, arg);
		clients[pid].sizedrecv.type = 0;
		clients[pid].sizedrecv.totallen = 0;
		clients[pid].sizedrecv.offset = 0;
	}
}

int QueueMoreData(void *dummy)
{
#define REQUESTATONCE (QUEUE_PACKETS*480)
	byte buffer[REQUESTATONCE], *dp;
	struct ReliablePacket packet;
	int pid, needed;
	Link *l;

	for (pid = 0; pid < MAXPACKET; pid++)
		if (players[pid].status > S_FREE &&
		    players[pid].status < S_TIMEWAIT &&
		    IS_OURS(pid) &&
		    pthread_mutex_trylock(outlistmtx + pid) == 0)
		{
			if ((l = LLGetHead(&clients[pid].sizedsends)) &&
			    DQCount(&clients[pid].outlist) < QUEUE_THRESHOLD)
			{
				struct sized_send_data *sd = l->data;

				/* unlock while we get the data */
				pthread_mutex_unlock(outlistmtx + pid);

				/* prepare packet */
				packet.t1 = 0x00;
				packet.t2 = 0x0A;
				packet.seqnum = sd->totallen;

				/* get needed bytes */
				needed = REQUESTATONCE;
				if ((sd->totallen - sd->offset) < needed)
					needed = sd->totallen - sd->offset;
				sd->request_data(sd->clos, sd->offset, buffer, needed);
				sd->offset += needed;

				/* now lock while we buffer it */
				pthread_mutex_lock(outlistmtx + pid);

				/* put data in outlist, in 480 byte chunks */
				dp = buffer;
				while (needed > 480)
				{
					memcpy(packet.data, dp, 480);
					BufferPacket(pid, (byte*)&packet, 486, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);
					dp += 480;
					needed -= 480;
				}
				memcpy(packet.data, dp, needed);
				BufferPacket(pid, (byte*)&packet, needed + 6, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);

				/* check if we need more */
				if (sd->offset >= sd->totallen)
				{
					/* notify sender that this is the end */
					sd->request_data(sd->clos, sd->offset, NULL, 0);
					LLRemove(&clients[pid].sizedsends, sd);
					afree(sd);
				}

			}
			pthread_mutex_unlock(outlistmtx + pid);
		}

	return TRUE;
}


/* call with outlistmtx locked */
local void send_outgoing(int pid)
{
	byte ubuf[MAXPACKET] = { 0x00, 0x0E };
	byte rbuf[MAXPACKET] = { 0x00, 0x0E };

	unsigned int gtc = GTC();
	int ucount = 0, rcount = 0, bytessince, pri;
	int retries = 0;
	Buffer *buf, *nbuf, *rebuf;
	byte *uptr = ubuf + 2;
	byte *rptr = rbuf + 2;
	DQNode *outlist;

	/* check if it's time to clear the bytessent */
	if ( (gtc - clients[pid].sincetime) >= CFG_BANDWIDTH_RES)
	{
		clients[pid].sincetime = gtc;
		clients[pid].bytessince = 0;
	}

	/* we keep a local copy of bytessince to account for the
	 * sizes of stuff in grouped packets. */
	bytessince = clients[pid].bytessince;

	/* iterate through outlist */
	outlist = &clients[pid].outlist;

	/* process highest priority first */
	for (pri = 7; pri > 0; pri--)
	{
		int limit = clients[pid].limit * pri_limits[pri] / 100;

		for (buf = (Buffer*)outlist->next;
		     (DQNode*)buf != outlist;
		     buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;
			if (buf->pri == pri &&
			    (gtc - buf->lastretry) > config.timeout &&
			    (bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
			{
				if (buf->lastretry != 0)
					/* this is a retry, not an initial send */
					retries++;

				/* sometimes, we have to not group a packet:
				 *   if it's len > 240
				 *   if it's reliable with a callback
				 */
				if (buf->len > 240 ||
				    (buf->reliable && buf->callback))
				{
					/* send immediately */
					/* check if it needs rebuffering */
					if (buf->reliable)
					{
						/* we should only rebuffer this if there are no
						 * reliable packets waiting in rbuf. if there
						 * are, we can wait until the next time around.
						 */
						if (rcount == 0)
						{
							/* now rebuffer it */
							rebuf = BufferPacket(pid, buf->d.raw, buf->len,
									NET_REALRELIABLE,
									buf->callback, buf->clos);

							/* send the rebuffered one once */
							bytessince += rebuf->len + IP_UDP_OVERHEAD;
							SendRaw(rebuf->pid, rebuf->d.raw, rebuf->len);
							rebuf->lastretry = gtc;

							DQRemove((DQNode*)buf);
							FreeBuffer(buf);
						}
					}
					else
					{
						/* just send it as is */
						bytessince += buf->len + IP_UDP_OVERHEAD;
						SendRaw(buf->pid, buf->d.raw, buf->len);
						buf->lastretry = gtc;
						if (! IS_REL(buf))
						{
							/* if we just sent an unreliable packet,
							 * free it so we don't send it again. */
							DQRemove((DQNode*)buf);
							FreeBuffer(buf);
						}
					}
				}
				/* now we have two cases for whether this is reliable or
				 * not (reliable packets that already have a seqnum are
				 * treated as unreliable here) */
				else if (buf->reliable)
				{
					if (((rptr - rbuf) + buf->len) < (MAXPACKET-16))
					{
						/* add to current reliable grouped packet, if
						 * there is room */
						*rptr++ = buf->len;
						memcpy(rptr, buf->d.raw, buf->len);
						rptr += buf->len;
						bytessince += buf->len + 1;
						rcount++;
						/* always remove these since we will re-buffer
						 * them below. */
						DQRemove((DQNode*)buf);
						FreeBuffer(buf);
					}
				}
				else
				{
					if (((uptr - ubuf) + buf->len) < (MAXPACKET-10))
					{
						/* add to current unreliable grouped packet, if there is room */
						*uptr++ = buf->len;
						memcpy(uptr, buf->d.raw, buf->len);
						uptr += buf->len;
						buf->lastretry = gtc;
						bytessince += buf->len + 1;
						ucount++;
						if (! IS_REL(buf))
						{
							DQRemove((DQNode*)buf);
							FreeBuffer(buf);
						}
					}
				}
			}
		}
	}

	/* try sending the unreliable grouped packet */
	if (ucount == 1)
	{
		/* there's only one in the group, so don't send it
		 * in a group. +3 to skip past the 00 0E and size of
		 * first packet */
		SendRaw(pid, ubuf + 3, (uptr - ubuf) - 3);
	}
	else if (ucount > 1)
	{
		/* send the whole thing as a group */
		SendRaw(pid, ubuf, uptr - ubuf);
	}

	/* now try the reliable grouped packet */
	if (rcount == 1)
	{
		/* only one in this group, just rebuffer it without grouped
		 * header */
		rebuf = BufferPacket(pid, rbuf + 3, (rptr - rbuf) - 3,
				NET_REALRELIABLE | NET_PRI_P1, NULL, NULL);
		SendRaw(pid, rebuf->d.raw, rebuf->len);
		rebuf->lastretry = gtc;
	}
	else if (rcount > 1)
	{
		rebuf = BufferPacket(pid, rbuf, rptr - rbuf,
				NET_REALRELIABLE | NET_PRI_P1, NULL, NULL);
		SendRaw(pid, rebuf->d.raw, rebuf->len);
		rebuf->lastretry = gtc;
	}

	clients[pid].retries += retries;
	submit_rel_stats(pid);
}


/* call with player status locked */
local void process_lagouts(int pid, unsigned int gtc)
{
	/* this is used for lagouts and also for timewait */
	int diff = (int)gtc - (int)clients[pid].lastpkt;

	/* process lagouts */
	if (players[pid].status != S_FREE &&
			players[pid].whenloggedin == 0 && /* acts as flag to prevent dups */
			clients[pid].lastpkt != 0 && /* prevent race */
			diff > config.droptimeout)
	{
		lm->Log(L_DRIVEL,
				"<net> [%s] Player kicked for no data (lagged off)",
				players[pid].name);
		/* FIXME: send "you have been disconnected..." msg */
		/* can't hold lock here for deadlock-related reasons */
		pd->UnlockStatus();
		KillConnection(pid);
		pd->LockStatus();
	}

	/* process timewait state */
	/* btw, status is locked in here */
	if (players[pid].status == S_TIMEWAIT)
	{
		char drop[2] = {0x00, 0x07};
		int bucket;

		/* here, send disconnection packet */
		SendToOne(pid, drop, 2, NET_PRI_P5);

		/* tell encryption to forget about him */
		if (clients[pid].enc)
		{
			clients[pid].enc->Void(pid);
			clients[pid].enc = NULL;
		}

		/* log message */
		lm->Log(L_INFO, "<net> [%s] [pid=%d] Disconnected",
				players[pid].name, pid);

		pthread_mutex_lock(&hashmtx);
		bucket = HashIP(clients[pid].sin);
		if (clienthash[bucket] == pid)
			clienthash[bucket] = clients[pid].nextinbucket;
		else
		{
			int j = clienthash[bucket];
			while (j >= 0 && clients[j].nextinbucket != pid)
				j = clients[j].nextinbucket;
			if (j >= 0)
				clients[j].nextinbucket = clients[pid].nextinbucket;
			else
			{
				lm->Log(L_ERROR, "<net> Internal error: "
						"established connection not in hash table");
			}
		}

		pd->FreePlayer(pid);

		pthread_mutex_unlock(&hashmtx);
	}
}


void * SendThread(void *dummy)
{
	unsigned int i, gtc;

	while (!killallthreads)
	{
		sched_yield();
		usleep(10000); /* 1/100 second */

		/* first send outgoing packets */
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
			if ( ((players[i].status > S_FREE && players[i].status < S_TIMEWAIT &&
			       IS_OURS(i)) ||
			     i >= MAXPLAYERS /* billing needs to send before connected */) &&
			     pthread_mutex_trylock(outlistmtx + i) == 0)
			{
				send_outgoing(i);
				pthread_mutex_unlock(outlistmtx + i);
			}

		/* process lagouts and timewait
		 * do this in another loop so that we only have to lock/unlock
		 * player status once. */
		pd->LockStatus();
		gtc = GTC();
		for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
			if (IS_OURS(i))
				process_lagouts(i, gtc);
		pd->UnlockStatus();
	}

	return NULL;
}


void * RelThread(void *dummy)
{
	Buffer *buf, *nbuf;
	int worked = 0, pid;

	pthread_mutex_lock(&relmtx);
	while (!killallthreads)
	{
		/* wait for reliable pkt to process */
		if (!worked)
			pthread_cond_wait(&relcond, &relmtx);

		worked = 0;
		for (buf = (Buffer*)rellist.next; (DQNode*)buf != &rellist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;
			pid = buf->pid;

			/* if player is gone, free buffer */
			if (players[pid].status <= S_FREE ||
			    players[pid].status >= S_TIMEWAIT )
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
				pthread_mutex_unlock(&relmtx);

				/* process it */
				ProcessPacket(pid, buf->d.raw, buf->len);

				FreeBuffer(buf);
				pthread_mutex_lock(&relmtx);
			}
#endif
			else if (buf->d.rel.seqnum == (clients[pid].c2sn + 1) )
			{
				/* else, if seqnum matches, process */
				clients[pid].c2sn++;
				DQRemove((DQNode*)buf);
				/* don't hold mutex while processing packet */
				pthread_mutex_unlock(&relmtx);

				/* process it */
				ProcessPacket(pid, buf->d.rel.data, buf->len - 6);

				FreeBuffer(buf);

				submit_rel_stats(pid);
				pthread_mutex_lock(&relmtx);
				worked = 1;
			}
			else if (buf->d.rel.seqnum <= clients[pid].c2sn)
			{
				/* this is a duplicated reliable packet */
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
				/* lag data */
				clients[pid].reldups++;
				submit_rel_stats(pid);
			}
		}
	}
	pthread_mutex_unlock(&relmtx);
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
	else if (buf->d.rel.t1 < MAXTYPES)
	{
		LinkedList *lst;
		Link *l;

		lst = buf->pid != PID_BILLER ? handlers : billhandlers;
		lst += (int)buf->d.rel.t1;

		pd->LockPlayer(buf->pid);
		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)(l->data))(buf->pid, buf->d.raw, buf->len);
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
	else if (d[0] < MAXTYPES)
	{
		LinkedList *lst;
		Link *l;
#ifdef CFG_DUMP_UNKNOWN_PACKETS
		int count = 0;
#endif

		lst = pid != PID_BILLER ? handlers : billhandlers;
		lst += d[0];

		pd->LockPlayer(pid);
#ifndef CFG_DUMP_UNKNOWN_PACKETS
		for (l = LLGetHead(lst); l; l = l->next)
#else
			for (l = LLGetHead(lst); l; l = l->next, count++)
#endif
				((PacketFunc)(l->data))(pid, d, len);
		pd->UnlockPlayer(pid);

#ifdef CFG_DUMP_UNKNOWN_PACKETS
		if (!count)
		{
			char str[256];
			int c, i;

			for (c = 0, i = 0; c < len && i < 250; c++, i += 3)
				sprintf(&str[i], "%02X ", d[c]);

			if (lm) lm->Log(L_DRIVEL, "<net> [pid=%d] Unknown packet (len %d): %s", pid, len, str);
		}
#endif
	}
}


int NewConnection(int type, struct sockaddr_in *sin, Iencrypt *enc)
{
	int i, bucket;

	if (sin)
	{
		pd->LockStatus();

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

		pd->UnlockStatus();
	}

	i = pd->NewPlayer(type);

	if (sin)
		lm->Log(L_DRIVEL,"<net> [pid=%d] New connection from %s:%i",
				i, inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	else
		lm->Log(L_DRIVEL,"<net> [pid=%d] New internal connection", i);

	InitClient(i, enc);

	/* add him to his hash bucket */
	pthread_mutex_lock(&hashmtx);
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
	}
	pthread_mutex_unlock(&hashmtx);

	return i;
}


void KillConnection(int pid)
{
	byte leaving = C2S_LEAVING;

	pd->LockPlayer(pid);

	/* check to see if he has any ongoing file transfers */
	end_sized(pid, 0);

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

		for (l = LLGetHead(billhandlers + 0); l; l = l->next)
			((PacketFunc)(l->data))(buf->pid, buf->d.raw, buf->len);
	}
	FreeBuffer(buf);
}


void ProcessReliable(Buffer *buf)
{
	/* calculate seqnum delta to decide if we want to ack it. relmtx
	 * protects the c2sn values in the clients array. */
	int sn = buf->d.rel.seqnum;

	pthread_mutex_lock(&relmtx);

	if ((sn - clients[buf->pid].c2sn) > config.bufferdelta)
	{
		/* just drop it */
		pthread_mutex_unlock(&relmtx);
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
		pthread_cond_signal(&relcond);
		pthread_mutex_unlock(&relmtx);

		/* send the ack. use priority 3 so it gets sent as soon as
		 * possible, but not urgent, because we want to combine multiple
		 * ones into packets. */
		pthread_mutex_lock(outlistmtx + buf->pid);
		BufferPacket(buf->pid, (byte*)&ack, sizeof(ack),
				NET_UNRELIABLE | NET_PRI_P3, NULL, NULL);
		pthread_mutex_unlock(outlistmtx + buf->pid);
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
	void *clos = NULL;

	unsigned int ping = 1;

	pthread_mutex_lock(outlistmtx + buf->pid);
	outlist = &clients[buf->pid].outlist;
	for (b = (Buffer*)outlist->next; (DQNode*)b != outlist; b = nbuf)
	{
		nbuf = (Buffer*)b->node.next;
		/* this should only match once */
		if (IS_REL(b) &&
		    b->d.rel.seqnum == buf->d.rel.seqnum)
		{
			callback = b->callback;
			clos = b->clos;
			ping = (GTC() - b->lastretry) * 10;
			DQRemove((DQNode*)b);
			FreeBuffer(b);
		}
	}
	pthread_mutex_unlock(outlistmtx + buf->pid);

	if (callback)
		callback(buf->pid, 1, clos);
	
	if (lagc && ping != 1)
		lagc->RelDelay(buf->pid, ping);

	FreeBuffer(buf);
}


void ProcessSyncRequest(Buffer *buf)
{
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)(buf->d.raw);
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time, GTC() };
	pthread_mutex_lock(outlistmtx + buf->pid);
	/* note: this bypasses bandwidth limits */
	SendRaw(buf->pid, (byte*)&ts, sizeof(ts));
	pthread_mutex_unlock(outlistmtx + buf->pid);

	/* submit data to lagdata */
	if (lagc)
	{
		struct ClientPLossData data;
		data.s_pktrcvd = clients[buf->pid].pktrecvd;
		data.s_pktsent = clients[buf->pid].pktsent;
		data.c_pktrcvd = cts->pktrecvd;
		data.c_pktsent = cts->pktsent;
		lagc->ClientPLoss(buf->pid, &data);
	}

	FreeBuffer(buf);
}


void ProcessDrop(Buffer *buf)
{
	KillConnection(buf->pid);
	FreeBuffer(buf);
}


void ProcessBigData(Buffer *buf)
{
	int newsize;
	byte *newbuf;
	ClientData *client = clients + buf->pid;

	pd->LockPlayer(buf->pid);

	newsize = client->bigrecv.size + buf->len - 2;

	if (newsize > MAXBIGPACKET)
	{
		lm->LogP(L_MALICIOUS, "net", buf->pid, "Refusing to allocate more than %d bytes", MAXBIGPACKET);
		goto freebigbuf;
	}

	if (client->bigrecv.room < newsize)
	{
		client->bigrecv.room *= 2;
		if (client->bigrecv.room < newsize)
			client->bigrecv.room = newsize;
		newbuf = realloc(client->bigrecv.buf, client->bigrecv.room);
		if (!newbuf)
		{
			lm->LogP(L_ERROR,"net", buf->pid, "Cannot allocate %d bytes for bigpacket", newsize);
			goto freebigbuf;
		}
		client->bigrecv.buf = newbuf;
	}
	else
		newbuf = client->bigrecv.buf;

	memcpy(newbuf + client->bigrecv.size, buf->d.raw + 2, buf->len - 2);

	client->bigrecv.buf = newbuf;
	client->bigrecv.size = newsize;

	if (buf->d.rel.t2 == 0x08) goto reallyexit;

	ProcessPacket(buf->pid, newbuf, newsize);

freebigbuf:
	afree(client->bigrecv.buf);
	client->bigrecv.buf = NULL;
	client->bigrecv.size = 0;
	client->bigrecv.room = 0;
reallyexit:
	pd->UnlockPlayer(buf->pid);
	FreeBuffer(buf);
}


void ProcessPresize(Buffer *buf)
{
	Link *l;
	ClientData *client = clients + buf->pid;
	int size = buf->d.rel.seqnum;

	pd->LockPlayer(buf->pid);

	if (client->sizedrecv.offset == 0)
	{
		/* first packet */
		if (buf->d.rel.data[0] < MAXTYPES)
		{
			client->sizedrecv.type = buf->d.rel.data[0];
			client->sizedrecv.totallen = size;
		}
		else
		{
			end_sized(buf->pid, 0);
			goto presized_done;
		}
	}

	if (clients->sizedrecv.totallen != size)
	{
		lm->LogP(L_MALICIOUS, "net", buf->pid, "Length mismatch in sized packet");
		end_sized(buf->pid, 0);
	}
	else if ((clients->sizedrecv.offset + buf->len - 6) > size)
	{
		lm->LogP(L_MALICIOUS, "net", buf->pid, "Sized packet overflow");
		end_sized(buf->pid, 0);
	}
	else
	{
		for (l = LLGetHead(sizedhandlers + client->sizedrecv.type); l; l = l->next)
			((SizedPacketFunc)(l->data))
				(buf->pid, buf->d.rel.data, buf->len - 6, client->sizedrecv.offset, size);

		clients->sizedrecv.offset += buf->len - 6;

		if (clients->sizedrecv.offset >= size)
			end_sized(buf->pid, 1);
	}

presized_done:
	pd->UnlockPlayer(buf->pid);
	FreeBuffer(buf);
}


void ProcessCancel(Buffer *req)
{
	/* the client has request a cancel for the file transfer. that means
	 * we have to go through the outlist and remove all presized
	 * packets. */

	int pid = req->pid;
	Buffer *buf, *nbuf;
	DQNode *outlist;

	pthread_mutex_lock(outlistmtx + pid);

	outlist = &clients[pid].outlist;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		/* callbacks aren't allowed for file transfers, so don't worry
		 * about them */
		nbuf = (Buffer*)buf->node.next;
		if (IS_PRESIZED(buf))
		{
			DQRemove((DQNode*)buf);
			FreeBuffer(buf);
		}
	}
	pthread_mutex_unlock(outlistmtx + pid);

	FreeBuffer(req);
}


void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len)
{
#ifdef CFG_DUMP_RAW_PACKETS
	printf("SENDRAW: %d bytes\n", len);
	dump_pk(pkt, len);
#endif
	sendto(serversock, pkt, len, 0,
			(struct sockaddr*)sin, sizeof(struct sockaddr_in));
}


/* IMPORTANT: anyone calling SendRaw MUST hold the outlistmtx for the
 * player that they're sending data to if you want bytessince to be
 * accurate. */
void SendRaw(int pid, byte *data, int len)
{
	byte encbuf[MAXPACKET];
	Iencrypt *enc = clients[pid].enc;

	if (!IS_OURS(pid)) return;

	if (pid != PID_BILLER)
	{
		memcpy(encbuf, data, len);

#ifdef CFG_DUMP_RAW_PACKETS
		printf("SEND: %d bytes to pid %d\n", len, pid);
		dump_pk(encbuf, len);
#endif

		if (enc)
			len = enc->Encrypt(pid, encbuf, len);

#ifdef CFG_DUMP_RAW_PACKETS
		printf("SEND: %d bytes (after encryption):\n", len);
		dump_pk(encbuf, len);
#endif

		sendto(serversock, encbuf, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));

	}
	else
	{
		sendto(clientsock, data, len, 0,
				(struct sockaddr*)&clients[pid].sin, sizeof(struct sockaddr_in));
	}

	clients[pid].bytessince += len + IP_UDP_OVERHEAD;
	clients[pid].bytesent += len + IP_UDP_OVERHEAD;
	clients[pid].pktsent++;
	global_stats.pktsent++;
}


/* must be called with outlist mutex! */
Buffer * BufferPacket(int pid, byte *data, int len, int flags,
		RelCallback callback, void *clos)
{
	Buffer *buf;
	int limit;

	if (!IS_OURS(pid))
		return NULL;

	assert(len < MAXPACKET);

	/* handle default priority */
	if (GET_PRI(flags) == 0) flags |= NET_PRI_DEFAULT;
	limit = clients[pid].limit * pri_limits[GET_PRI(flags)] / 100;

	/* try the fast path */
	if (flags == NET_PRI_P4 || flags == NET_PRI_P5)
		if (clients[pid].bytessince + len + IP_UDP_OVERHEAD <= limit)
		{
			SendRaw(pid, data, len);
			return NULL;
		}

	buf = GetBuffer();

	buf->pid = pid;
	buf->lastretry = 0;
	buf->pri = GET_PRI(flags);
	buf->reliable = 0;
	buf->callback = callback;
	buf->clos = clos;
	global_stats.pri_stats[buf->pri]++;

	/* get data into packet */
	if (flags & NET_REALRELIABLE ||
			(flags & NET_RELIABLE && GET_PRI(flags) > 5))
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
		if (flags & NET_RELIABLE)
			buf->reliable = 1;
	}

	/* add it to out list */
	DQAdd(&clients[pid].outlist, (DQNode*)buf);

	/* if it's urgent, do one retry now */
	if (GET_PRI(flags) > 5)
		if ((clients[pid].bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
		{
			SendRaw(pid, buf->d.raw, buf->len);
			buf->lastretry = GTC();
		}

	return buf;
}


void SendToOne(int pid, byte *data, int len, int flags)
{
	/* see if we can do it the quick way */
	if (len < MAXPACKET)
	{
		pthread_mutex_lock(outlistmtx + pid);
		BufferPacket(pid, data, len, flags, NULL, NULL);
		pthread_mutex_unlock(outlistmtx + pid);
	}
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
		if (players[i].status == S_PLAYING &&
				players[i].arena == arena &&
				i != except)
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


void SendToTarget(const Target *target, byte *data, int len, int flags)
{
	int set[MAXPLAYERS+1];
	pd->TargetToSet(target, set);
	SendToSet(set, data, len, flags);
}


void SendToSet(int *set, byte *data, int len, int flags)
{
	if (len > MAXPACKET)
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
		SendToSet(set, buf, len+2, flags | NET_RELIABLE);
	}
	else
		for ( ; *set != -1; set++)
		{
			pthread_mutex_lock(outlistmtx + *set);
			BufferPacket(*set, data, len, flags, NULL, NULL);
			pthread_mutex_unlock(outlistmtx + *set);
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

	for ( ; *set != -1; set++)
	{
		pthread_mutex_lock(outlistmtx + *set);
		BufferPacket(*set, data, len, NET_RELIABLE, callback, clos);
		pthread_mutex_unlock(outlistmtx + *set);
	}
}


void SendSized(int pid, void *clos, int len,
		void (*req)(void *clos, int offset, byte *buf, int needed))
{
	struct sized_send_data *sd = amalloc(sizeof(*sd));

	sd->request_data = req;
	sd->clos = clos;
	sd->totallen = len;
	sd->offset = 0;

	pthread_mutex_lock(outlistmtx + pid);
	LLAdd(&clients[pid].sizedsends, sd);
	pthread_mutex_unlock(outlistmtx + pid);
}


i32 GetIP(int pid)
{
	return clients[pid].sin.sin_addr.s_addr;
}


void SetLimit(int pid, int limit)
{
	clients[pid].limit = limit * CFG_BANDWIDTH_RES / 100;
}


void GetStats(struct net_stats *stats)
{
	if (stats)
		*stats = global_stats;
}

void GetClientStats(int pid, struct net_client_stats *stats)
{
	ClientData *client = clients + pid;

	if (!stats || PID_BAD(pid)) return;

#define ASSIGN(field) stats->field = client->field
	ASSIGN(s2cn); ASSIGN(c2sn);
	ASSIGN(pktsent); ASSIGN(pktrecvd); ASSIGN(bytesent); ASSIGN(byterecvd);
#undef ASSIGN
	/* encryption */
	if (clients[pid].enc)
		stats->encname = clients[pid].enc->head.name;
	else
		stats->encname = "none";
	/* convert to bytes per second */
	stats->limit = clients->limit * 100 / CFG_BANDWIDTH_RES;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(clients[pid].sin.sin_addr), 16);
	stats->port = clients[pid].sin.sin_port;
}

