
/* dist: public */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
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
#include "encrypt.h"
#include "billcore.h"

/* defines */

#define MAXTYPES 64

/* ip/udp overhead, in bytes per physical packet */
#define IP_UDP_OVERHEAD 28

/* packets to queue up for sending files */
#define QUEUE_PACKETS 10
/* threshold to start queuing up more packets */
#define QUEUE_THRESHOLD 5

/* we need to know how many packets the client is able to buffer */
#define CLIENT_CAN_BUFFER 10

/* check whether we manage this client */
#define IS_OURS(p) ((p)->type == T_CONT || (p)->type == T_VIE)

/* check if a buffer is reliable */
#define IS_REL(buf) ((buf)->d.rel.t2 == 0x03 && (buf)->d.rel.t1 == 0x00)

/* check if a buffer is presized */
#define IS_PRESIZED(buf) \
	(IS_REL(buf) && \
	 (buf)->d.rel.data[1] == 0x0A && \
	 (buf)->d.rel.data[0] == 0x00)

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

typedef struct
{
	/* the address to send packets to */
	struct sockaddr_in sin;
	/* hash bucket */
	Player *nextinbucket;
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
		int type;
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
	pthread_mutex_t olmtx;
} ClientData; /* 136 bytes */



typedef struct Buffer
{
	DQNode node;
	/* p, len: valid for all buffers */
	/* pri, lastretry: valid for buffers in outlist */
	Player *p;
	int len, pri;
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
local void SendToOne(Player *, byte *, int, int);
local void SendToArena(Arena *, Player *, byte *, int, int);
local void SendToSet(LinkedList *, byte *, int, int);
local void SendToTarget(const Target *, byte *, int, int);
local void SendWithCallback(Player *p, byte *data, int length,
		RelCallback callback, void *clos);
local void SendSized(Player *p, void *clos, int len,
		void (*req)(void *clos, int offset, byte *buf, int needed));

local void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len);
local void AddPacket(int, PacketFunc);
local void RemovePacket(int, PacketFunc);
local void AddSizedPacket(int, SizedPacketFunc);
local void RemoveSizedPacket(int, SizedPacketFunc);
local Player * NewConnection(int type, struct sockaddr_in *, Iencrypt *enc);
local void SetLimit(Player *p, int limit);
local void GetStats(struct net_stats *stats);
local void GetClientStats(Player *p, struct net_client_stats *stats);
local int GetLastPacketTime(Player *p);

/* internal: */
local inline int HashIP(struct sockaddr_in);
local inline Player * LookupIP(struct sockaddr_in);
local inline void SendRaw(Player *, byte *, int);
local void KillConnection(Player *p);
local void ProcessBuffer(Buffer *);
local int InitSockets(void);
local Buffer * GetBuffer(void);
local Buffer * BufferPacket(Player *p, byte *data, int len, int flags,
		RelCallback callback, void *clos);
local void FreeBuffer(Buffer *);

/* threads: */
local void * RecvThread(void *);
local void * SendThread(void *);
local void * RelThread(void *);

local int QueueMoreData(void *);

/* network layer header handling: */
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

local LinkedList handlers[MAXTYPES];
local LinkedList sizedhandlers[MAXTYPES];

local int serversock = -1, pingsock = -1;

local DQNode freelist, rellist;
local pthread_mutex_t freemtx, relmtx;
local pthread_cond_t relcond;
volatile int killallthreads = 0;

local int clikey;
local Player * clienthash[CFG_HASHSIZE];
local pthread_mutex_t hashmtx;

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
	int port /* host order */, timeout, droptimeout;
	int bufferdelta, deflimit;
	unsigned long int bindaddr /* network order */;
} config;

local volatile struct net_stats global_stats;

local void (*oohandlers[])(Buffer*) =
{
	NULL, /* 00 - nothing */
	NULL, /* 01 - key initiation */
	NULL, /* 02 - key response (to be used for billing server) */
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

local Inet netint =
{
	INTERFACE_HEAD_INIT(I_NET, "net-udp")

	SendToOne, SendToArena, SendToSet, SendToTarget,
	SendWithCallback, SendSized,

	KillConnection,

	AddPacket, RemovePacket, AddSizedPacket, RemoveSizedPacket,

	ReallyRawSend, NewConnection,

	SetLimit, GetStats, GetClientStats, GetLastPacketTime,
};



/* start of functions */


EXPORT int MM_net(int action, Imodman *mm_, Arena *a)
{
	int i;
	pthread_t thd;
	const char *addr;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		if (!pd || !cfg || !lm || !ml) return MM_FAIL;

		clikey = pd->AllocatePlayerData(sizeof(ClientData));
		if (clikey == -1) return MM_FAIL;

		/* store configuration params */
		/* cfghelp: Net:Port, global, int, def: 5000
		 * The main port that the server runs on. */
		config.port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
		/* cfghelp: Net:ReliableTimeout, global, int, def: 100
		 * How long to wait to resend reliable packets (in ticks). */
		config.timeout = cfg->GetInt(GLOBAL, "Net", "ReliableTimeout", 100);
		/* cfghelp: Net:DropTimeout, global, int, def: 3000
		 * How long to get no data from a cilent before disconnecting
		 * him (in ticks). */
		config.droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		/* cfghelp: Net:MaxBufferDelta, global, int, def: 30
		 * The maximum number of reliable packets to buffer for a player. */
		config.bufferdelta = cfg->GetInt(GLOBAL, "Net", "MaxBufferDelta", 30);
		/* cfghelp: Net:BandwidthLimit, global, int, def: 3500
		 * The maximum number of bytes per second to send to each
		 * player by default. */
		config.deflimit = cfg->GetInt(GLOBAL, "Net", "BandwidthLimit", 3500);
		/* cfghelp: Net:BindIP, global, string
		 * If this is set, it must be a single IP address that the
		 * server should bind to. If unset, the server will bind to all
		 * available addresses. */
		addr = cfg->GetStr(GLOBAL, "Net", "BindIP");
		config.bindaddr = addr ? inet_addr(addr) : INADDR_ANY;

		/* get the sockets */
		if (InitSockets())
			return MM_FAIL;

		for (i = 0; i < MAXTYPES; i++)
		{
			LLInit(handlers + i);
			LLInit(sizedhandlers + i);
		}

		/* init hash and mutexes */
		for (i = 0; i < CFG_HASHSIZE; i++)
			clienthash[i] = NULL;
		pthread_mutex_init(&hashmtx, NULL);

		/* init buffers */
		pthread_cond_init(&relcond, NULL);
		pthread_mutex_init(&freemtx, NULL);
		pthread_mutex_init(&relmtx, NULL);
		DQInit(&freelist);
		DQInit(&rellist);

		/* start the threads */
		pthread_create(&thd, NULL, RecvThread, NULL);
		pthread_detach(thd);
		pthread_create(&thd, NULL, SendThread, NULL);
		pthread_detach(thd);
		pthread_create(&thd, NULL, RelThread, NULL);
		pthread_detach(thd);

		ml->SetTimer(QueueMoreData, 200, 150, NULL, NULL);

		/* install ourself */
		mm->RegInterface(&netint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		Link *link;
		Player *p;
		ClientData *cli;

		/* uninstall ourself */
		if (mm->UnregInterface(&netint, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(QueueMoreData, NULL);

		/* disconnect all clients nicely */
		pd->Lock();
		FOR_EACH_PLAYER_P(p, cli, clikey)
			if (IS_OURS(p))
			{
				byte discon[2] = { 0x00, 0x07 };
				cli->enc = NULL;
				SendRaw(p, discon, 2);
			}
		pd->Unlock();

		/* clean up */
		for (i = 0; i < MAXTYPES; i++)
		{
			LLEmpty(handlers + i);
			LLEmpty(sizedhandlers + i);
		}

		/* let threads die */
		killallthreads = 1;
		/* note: we don't join them because they could be blocked on
		 * something, and who ever wants to unload net anyway? */

		close(serversock);
		close(pingsock);

		pd->FreePlayerData(clikey);

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
}
void RemovePacket(int t, PacketFunc f)
{
	if (t >= 0 && t < MAXTYPES)
		LLRemove(handlers+t, f);
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

Player * LookupIP(struct sockaddr_in sin)
{
	int hashbucket = HashIP(sin);
	Player *p;

	pthread_mutex_lock(&hashmtx);
	p = clienthash[hashbucket];
	while (p)
	{
		ClientData *cli = PPDATA(p, clikey);
		if (cli->sin.sin_addr.s_addr == sin.sin_addr.s_addr &&
		    cli->sin.sin_port == sin.sin_port)
			break;
		p = cli->nextinbucket;
	}
	pthread_mutex_unlock(&hashmtx);
	return p;
}


local void ClearOutlist(Player *p)
{
	ClientData *cli = PPDATA(p, clikey);
	DQNode *outlist = &cli->outlist;
	Buffer *buf, *nbuf;
	Link *l;

	pthread_mutex_lock(&cli->olmtx);

	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		nbuf = (Buffer*)buf->node.next;
		if (buf->callback)
		{
			/* this is ugly, but we have to release the outlist mutex
			 * during these callbacks, because the callback might need
			 * to acquire some mutexes of its own, and we want to avoid
			 * deadlock. */
			pthread_mutex_unlock(&cli->olmtx);
			buf->callback(p, 0, buf->clos);
			pthread_mutex_lock(&cli->olmtx);
		}
		DQRemove((DQNode*)buf);
		FreeBuffer(buf);
	}

	for (l = LLGetHead(&cli->sizedsends); l; l = l->next)
	{
		struct sized_send_data *sd = l->data;
		sd->request_data(sd->clos, 0, NULL, 0);
		afree(sd);
	}
	LLEmpty(&cli->sizedsends);

	pthread_mutex_unlock(&cli->olmtx);
}


local void InitClient(ClientData *cli, Iencrypt *enc)
{
	/* set up clientdata */
	memset(cli, 0, sizeof(ClientData));
	pthread_mutex_init(&cli->olmtx, NULL);
	cli->c2sn = -1;
	cli->limit = config.deflimit;
	cli->enc = enc;
	LLInit(&cli->sizedsends);
	DQInit(&cli->outlist);
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
	localsin.sin_addr.s_addr = config.bindaddr;
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

	return 0;
}


#if 0 && (defined(CFG_DUMP_RAW_PACKETS) || defined(CFG_DUMP_UNKNOWN_PACKETS))
local void dump_pk(byte *d, int len)
{
	char str[256];
	int c, i;
	for (c = 0, i = 0; c < len && i < 250; c++, i += 3)
		sprintf(&str[i], "%02X ", d[c]);
	puts(str);
}
#endif


void * RecvThread(void *dummy)
{
	struct sockaddr_in sin;
	struct timeval tv;
	fd_set fds;
	int len, sinsize, maxfd = 5;
	Player *p;
	ClientData *cli;

	while (!killallthreads)
	{
		do {
			/* set up fd set and tv */
			FD_ZERO(&fds);
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

			/* search for an existing connection */
			p = LookupIP(sin);

			if (p == NULL)
			{
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
			status = p->status;
			cli = PPDATA(p, clikey);

			if (IS_CONNINIT(buf))
			{
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
					KillConnection(p);
				}
				goto freebuf;
			}

			/* we shouldn't get packets in this state, but it's harmless
			 * if we do. */
			if (status == S_TIMEWAIT)
				goto freebuf;

			if (status > S_TIMEWAIT)
			{
				lm->Log(L_WARN, "<net> [pid=%d] Packet recieved from bad state %d", p->pid, status);
				goto freebuf;
				/* don't set lastpkt time here */
			}

			buf->p = p;
			cli->lastpkt = GTC();
			cli->byterecvd += len + IP_UDP_OVERHEAD;
			cli->pktrecvd++;
			global_stats.pktrecvd++;

			/* decrypt the packet */
			{
				Iencrypt *enc = cli->enc;
				if (enc)
					len = enc->Decrypt(p, buf->d.raw, len);
			}

			if (len != 0)
				buf->len = len;
			else /* bad crc, or something */
			{
				lm->Log(L_MALICIOUS, "<net> [pid=%d] "
						"failure decrypting packet", p->pid);
				goto freebuf;
			}

#ifdef CFG_DUMP_RAW_PACKETS
			printf("RECV: about to process %d bytes:\n", len);
			dump_pk(buf->d.raw, len);
#endif

			/* hand it off to appropriate place */
			ProcessBuffer(buf);

			goto donehere;
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
			len = recvfrom(pingsock, (char*)data, 4, 0,
					(struct sockaddr*)&sin, &sinsize);

			if (len == 4)
			{
				Link *link;
				data[1] = data[0];
				data[0] = 0;
				pd->Lock();
				FOR_EACH_PLAYER(p)
					if (p->status == S_PLAYING &&
					    p->type != T_FAKE)
						data[0]++;
				pd->Unlock();
				sendto(pingsock, (char*)data, 8, 0,
						(struct sockaddr*)&sin, sinsize);

				global_stats.pcountpings++;
			}
		}
	}
	return NULL;
}


local void submit_rel_stats(Player *p)
{
	if (lagc)
	{
		ClientData *cli = PPDATA(p, clikey);
		struct ReliableLagData rld;
		rld.reldups = cli->reldups;
		/* the plus one is because c2sn is the rel id of the last packet
		 * that we've seen, not the one we want to see. */
		rld.c2sn = cli->c2sn + 1;
		rld.retries = cli->retries;
		rld.s2cn = cli->s2cn;
		lagc->RelStats(p, &rld);
	}
}


local void end_sized(Player *p, int success)
{
	ClientData *cli = PPDATA(p, clikey);
	if (cli->sizedrecv.offset != 0)
	{
		Link *l;
		u8 type = cli->sizedrecv.type;
		int arg = success ? cli->sizedrecv.totallen : -1;
		/* tell listeners that they're cancelled */
		if (type < MAXTYPES)
			for (l = LLGetHead(sizedhandlers + type); l; l = l->next)
				((SizedPacketFunc)(l->data))(p, NULL, 0, arg, arg);
		cli->sizedrecv.type = 0;
		cli->sizedrecv.totallen = 0;
		cli->sizedrecv.offset = 0;
	}
}


int QueueMoreData(void *dummy)
{
#define REQUESTATONCE (QUEUE_PACKETS*480)
	byte buffer[REQUESTATONCE], *dp;
	struct ReliablePacket packet;
	int needed;
	Link *link, *l;
	Player *p;
	ClientData *cli;

	FOR_EACH_PLAYER_P(p, cli, clikey)
		if (IS_OURS(p) &&
		    p->status < S_TIMEWAIT &&
		    pthread_mutex_trylock(&cli->olmtx) == 0)
		{
			if ((l = LLGetHead(&cli->sizedsends)) &&
			    DQCount(&cli->outlist) < QUEUE_THRESHOLD)
			{
				struct sized_send_data *sd = l->data;

				/* unlock while we get the data */
				pthread_mutex_unlock(&cli->olmtx);

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
				pthread_mutex_lock(&cli->olmtx);

				/* put data in outlist, in 480 byte chunks */
				dp = buffer;
				while (needed > 480)
				{
					memcpy(packet.data, dp, 480);
					BufferPacket(p, (byte*)&packet, 486, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);
					dp += 480;
					needed -= 480;
				}
				if (needed > 0)
				{
					memcpy(packet.data, dp, needed);
					BufferPacket(p, (byte*)&packet, needed + 6, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);

				}

				/* check if we need more */
				if (sd->offset >= sd->totallen)
				{
					/* notify sender that this is the end */
					sd->request_data(sd->clos, sd->offset, NULL, 0);
					LLRemove(&cli->sizedsends, sd);
					afree(sd);
				}

			}
			pthread_mutex_unlock(&cli->olmtx);
		}

	return TRUE;
}


/* call with outlistmtx locked */
local void send_outgoing(Player *p)
{
	ClientData *cli = PPDATA(p, clikey);
	byte gbuf[MAXPACKET] = { 0x00, 0x0E };
	byte *gptr = gbuf + 2;

	unsigned int gtc = GTC();
	int gcount = 0, bytessince, pri, retries = 0, minseqnum;
	Buffer *buf, *nbuf;
	DQNode *outlist;

	/* check if it's time to clear the bytessent */
	if ( (gtc - cli->sincetime) >= CFG_BANDWIDTH_RES)
	{
		cli->sincetime = gtc;
		cli->bytessince = 0;
	}

	/* we keep a local copy of bytessince to account for the
	 * sizes of stuff in grouped packets. */
	bytessince = cli->bytessince;

	outlist = &cli->outlist;

	/* find smallest seqnum remaining in outlist */
	minseqnum = INT_MAX;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = (Buffer*)buf->node.next)
		if (IS_REL(buf) &&
		    buf->d.rel.seqnum < minseqnum)
			minseqnum = buf->d.rel.seqnum;

	/* process highest priority first */
	for (pri = 7; pri > 0; pri--)
	{
		int limit = cli->limit * pri_limits[pri] / 100;

		for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;

			if (buf->pri == pri &&
			    (gtc - buf->lastretry) > config.timeout &&
			    ( ! IS_REL(buf) ||
			      (buf->d.rel.seqnum - minseqnum) < CLIENT_CAN_BUFFER ) &&
			    (bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
			{
				if (buf->lastretry != 0)
					/* this is a retry, not an initial send */
					retries++;

				/* try to group it */
				if (buf->len <= 255 &&
				    buf->callback == NULL &&
				    ((gptr - gbuf) + buf->len) < (MAXPACKET-10))
				{
					*gptr++ = buf->len;
					memcpy(gptr, buf->d.raw, buf->len);
					gptr += buf->len;
					buf->lastretry = gtc;
					bytessince += buf->len + 1;
					gcount++;
					if (! IS_REL(buf))
					{
						DQRemove((DQNode*)buf);
						FreeBuffer(buf);
					}
				}
				else
				{
					/* send immediately */
					bytessince += buf->len + IP_UDP_OVERHEAD;
					SendRaw(p, buf->d.raw, buf->len);
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
		}
	}

	/* try sending the grouped packet */
	if (gcount == 1)
	{
		/* there's only one in the group, so don't send it
		 * in a group. +3 to skip past the 00 0E and size of
		 * first packet */
		SendRaw(p, gbuf + 3, (gptr - gbuf) - 3);
	}
	else if (gcount > 1)
	{
		/* send the whole thing as a group */
		SendRaw(p, gbuf, gptr - gbuf);
	}

	cli->retries += retries;
}


/* call with player status locked */
local void process_lagouts(Player *p, unsigned int gtc)
{
	ClientData *cli = PPDATA(p, clikey);
	/* this is used for lagouts and also for timewait */
	int diff = gtc - cli->lastpkt;

	/* process lagouts */
	if (p->whenloggedin == 0 && /* acts as flag to prevent dups */
	    cli->lastpkt != 0 && /* prevent race */
	    diff > config.droptimeout)
	{
		lm->Log(L_DRIVEL,
				"<net> [%s] [pid=%d] Player kicked for no data (lagged off)",
				p->name, p->pid);
		/* FIXME: send "you have been disconnected..." msg */
		/* can't hold lock here for deadlock-related reasons */
		pd->Unlock();
		KillConnection(p);
		pd->Lock();
	}

	/* process timewait state */
	/* btw, status is locked in here */
	if (p->status == S_TIMEWAIT)
	{
		char drop[2] = {0x00, 0x07};
		int bucket;

		/* here, send disconnection packet */
		SendToOne(p, drop, 2, NET_PRI_P5);

		/* tell encryption to forget about him */
		if (cli->enc)
		{
			cli->enc->Void(p);
			cli->enc = NULL;
		}

		/* log message */
		lm->Log(L_INFO, "<net> [%s] [pid=%d] Disconnected",
				p->name, p->pid);

		pthread_mutex_lock(&hashmtx);
		bucket = HashIP(cli->sin);
		if (clienthash[bucket] == p)
			clienthash[bucket] = cli->nextinbucket;
		else
		{
			Player *j = clienthash[bucket];
			ClientData *jcli = PPDATA(j, clikey);

			while (j && jcli->nextinbucket != p)
				j = jcli->nextinbucket;
			if (j)
				jcli->nextinbucket = cli->nextinbucket;
			else
			{
				lm->Log(L_ERROR, "<net> Internal error: "
						"established connection not in hash table");
			}
		}
		pthread_mutex_unlock(&hashmtx);

		/* one more time, just to be sure */
		ClearOutlist(p);
		pthread_mutex_destroy(&cli->olmtx);

		pd->FreePlayer(p);
	}
}


void * SendThread(void *dummy)
{
	unsigned int gtc;

	while (!killallthreads)
	{
		ClientData *cli;
		Player *p;
		Link *link;

		sched_yield();
		usleep(10000); /* 1/100 second */

		/* first send outgoing packets */
		FOR_EACH_PLAYER_P(p, cli, clikey);
			if (p->status < S_TIMEWAIT &&
			    IS_OURS(p) &&
			    pthread_mutex_trylock(&cli->olmtx) == 0)
			{
				send_outgoing(p);
				submit_rel_stats(p);
				pthread_mutex_unlock(&cli->olmtx);
			}

		/* process lagouts and timewait
		 * do this in another loop so that we only have to lock/unlock
		 * player status once. */
		pd->Lock();
		gtc = GTC();
		FOR_EACH_PLAYER(p)
			if (IS_OURS(p))
				process_lagouts(p, gtc);
		pd->Unlock();
	}

	return NULL;
}


void * RelThread(void *dummy)
{
	Buffer *buf, *nbuf;
	int worked = 0;
	ClientData *cli;

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
			cli = PPDATA(buf->p, clikey);

			/* if player is gone, free buffer */
			if (buf->p->status >= S_TIMEWAIT)
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
			else if (buf->d.rel.seqnum == (cli->c2sn + 1) )
			{
				/* else, if seqnum matches, process */
				cli->c2sn++;
				DQRemove((DQNode*)buf);
				/* don't hold mutex while processing packet */
				pthread_mutex_unlock(&relmtx);

				/* process it */
				buf->len -= 6;
				memmove(buf->d.raw, buf->d.rel.data, buf->len);
				ProcessBuffer(buf);

				submit_rel_stats(buf->p);
				pthread_mutex_lock(&relmtx);
				worked = 1;
			}
			else if (buf->d.rel.seqnum <= cli->c2sn)
			{
				/* this is a duplicated reliable packet */
				DQRemove((DQNode*)buf);
				FreeBuffer(buf);
				/* lag data */
				cli->reldups++;
				submit_rel_stats(buf->p);
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
			lm->Log(L_MALICIOUS, "<net> [%s] [pid=%d] unknown network subtype %d",
					buf->p->name, buf->p->pid, buf->d.rel.t2);
			FreeBuffer(buf);
		}
	}
	else if (buf->d.rel.t1 >= MAXTYPES)
	{
		lm->Log(L_MALICIOUS, "<net> [%s] [pid=%d] unknown packet type %d",
				buf->p->name, buf->p->pid, buf->d.rel.t1);
	}
	else
	{
		LinkedList *lst = handlers + (int)buf->d.rel.t1;
		Link *l;

		pd->LockPlayer(buf->p);
		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)(l->data))(buf->p, buf->d.raw, buf->len);
		pd->UnlockPlayer(buf->p);

		FreeBuffer(buf);
	}
}


Player * NewConnection(int type, struct sockaddr_in *sin, Iencrypt *enc)
{
	int bucket;
	Player *p;
	ClientData *cli;

	/* try to find this sin in the hash table */
	if (sin && (p = LookupIP(*sin)))
	{
		/* we found it. if its status is S_CONNECTED, just return the
		 * pid. it means we have to redo part of the connection init. */
		if (p->status == S_CONNECTED)
			return p;
		else
		{
			/* otherwise, something is horribly wrong. make a note to
			 * this effect. */
			lm->Log(L_ERROR, "<net> [pid=%d] NewConnection called for an established address",
					p->pid);
			return NULL;
		}
	}

	p = pd->NewPlayer(type);
	cli = PPDATA(p, clikey);

	InitClient(cli, enc);

	/* add him to his hash bucket */
	pthread_mutex_lock(&hashmtx);
	if (sin)
	{
		memcpy(&cli->sin, sin, sizeof(struct sockaddr_in));
		bucket = HashIP(*sin);
		cli->nextinbucket = clienthash[bucket];
		clienthash[bucket] = p;
	}
	else
	{
		cli->nextinbucket = NULL;
	}
	pthread_mutex_unlock(&hashmtx);

	if (sin)
		lm->Log(L_DRIVEL,"<net> [pid=%d] New connection from %s:%i",
				p->pid, inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	else
		lm->Log(L_DRIVEL,"<net> [pid=%d] New internal connection", p->pid);

	return p;
}


void KillConnection(Player *p)
{
	pd->LockPlayer(p);

	/* check to see if he has any ongoing file transfers */
	end_sized(p, 0);

	/* if we haven't processed the leaving arena packet yet (quite
	 * likely), just generate one and process it. this will set status
	 * to S_LEAVING_ARENA */
	if (p->arena)
	{
		Iarenaman *aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		if (aman) aman->LeaveArena(p);
		mm->ReleaseInterface(aman);
	}

	pd->Lock();

	/* make sure that he's on his way out, in case he was kicked before
	 * fully logging in. */
	if (p->status < S_LEAVING_ARENA)
		p->status = S_LEAVING_ZONE;

	/* set status */
	/* set this special flag so that the player will be set to leave
	 * the zone when the S_LEAVING_ARENA-initiated actions are
	 * completed. */
	p->whenloggedin = S_LEAVING_ZONE;

	pd->Unlock();
	pd->UnlockPlayer(p);

	/* remove outgoing packets from the queue. this partially eliminates
	 * the need for a timewait state. */
	ClearOutlist(p);
}


void ProcessReliable(Buffer *buf)
{
	/* calculate seqnum delta to decide if we want to ack it. relmtx
	 * protects the c2sn values in the clients array. */
	int sn = buf->d.rel.seqnum;
	ClientData *cli = PPDATA(buf->p, clikey);

	pthread_mutex_lock(&relmtx);

	if ((sn - cli->c2sn) > config.bufferdelta)
	{
		/* just drop it */
		pthread_mutex_unlock(&relmtx);
		lm->Log(L_DRIVEL, "<net> [%s] [pid=%d] Reliable packet with too big delta (%d - %d)",
				buf->p->name, buf->p->pid, sn, cli->c2sn);
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
		pthread_mutex_lock(&cli->olmtx);
		BufferPacket(buf->p, (byte*)&ack, sizeof(ack),
				NET_UNRELIABLE | NET_PRI_P3, NULL, NULL);
		pthread_mutex_unlock(&cli->olmtx);
	}
}


void ProcessGrouped(Buffer *buf)
{
	int pos = 2, len = 1;

	while (pos < buf->len && len > 0)
	{
		len = buf->d.raw[pos++];
		if (pos + len <= buf->len)
		{
			Buffer *b = GetBuffer();
			b->p = buf->p;
			b->len = len;
			memcpy(b->d.raw, buf->d.raw + pos, len);
			ProcessBuffer(b);
		}
		pos += len;
	}
	FreeBuffer(buf);
}


void ProcessAck(Buffer *buf)
{
	ClientData *cli = PPDATA(buf->p, clikey);
	Buffer *b, *nbuf;
	DQNode *outlist;

	RelCallback callback = NULL;
	void *clos = NULL;

	unsigned int ping = 1;

	pthread_mutex_lock(&cli->olmtx);
	outlist = &cli->outlist;
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
	pthread_mutex_unlock(&cli->olmtx);

	if (callback)
		callback(buf->p, 1, clos);

	if (lagc && ping != 1)
		lagc->RelDelay(buf->p, ping);

	FreeBuffer(buf);
}


void ProcessSyncRequest(Buffer *buf)
{
	ClientData *cli = PPDATA(buf->p, clikey);
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)(buf->d.raw);
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time, GTC() };
	pthread_mutex_lock(&cli->olmtx);
	/* note: this bypasses bandwidth limits */
	SendRaw(buf->p, (byte*)&ts, sizeof(ts));
	pthread_mutex_unlock(&cli->olmtx);

	/* submit data to lagdata */
	if (lagc)
	{
		struct ClientPLossData data;
		data.s_pktrcvd = cli->pktrecvd;
		data.s_pktsent = cli->pktsent;
		data.c_pktrcvd = cts->pktrecvd;
		data.c_pktsent = cts->pktsent;
		lagc->ClientPLoss(buf->p, &data);
	}

	FreeBuffer(buf);
}


void ProcessDrop(Buffer *buf)
{
	KillConnection(buf->p);
	FreeBuffer(buf);
}


void ProcessBigData(Buffer *buf)
{
	ClientData *cli = PPDATA(buf->p, clikey);
	int newsize;
	byte *newbuf;

	pd->LockPlayer(buf->p);

	newsize = cli->bigrecv.size + buf->len - 2;

	if (newsize > MAXBIGPACKET)
	{
		lm->LogP(L_MALICIOUS, "net", buf->p, "Refusing to allocate more than %d bytes", MAXBIGPACKET);
		goto freebigbuf;
	}

	if (cli->bigrecv.room < newsize)
	{
		cli->bigrecv.room *= 2;
		if (cli->bigrecv.room < newsize)
			cli->bigrecv.room = newsize;
		newbuf = realloc(cli->bigrecv.buf, cli->bigrecv.room);
		if (!newbuf)
		{
			lm->LogP(L_ERROR,"net", buf->p, "Cannot allocate %d bytes for bigpacket", newsize);
			goto freebigbuf;
		}
		cli->bigrecv.buf = newbuf;
	}
	else
		newbuf = cli->bigrecv.buf;

	memcpy(newbuf + cli->bigrecv.size, buf->d.raw + 2, buf->len - 2);

	cli->bigrecv.buf = newbuf;
	cli->bigrecv.size = newsize;

	if (buf->d.rel.t2 == 0x08) goto reallyexit;

	if (newbuf[0] > 0 && newbuf[0] < MAXTYPES)
	{
		LinkedList *lst = handlers + (int)newbuf[0];
		Link *l;
		pd->LockPlayer(buf->p);
		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)(l->data))(buf->p, newbuf, newsize);
		pd->UnlockPlayer(buf->p);
	}
	else
		lm->LogP(L_WARN, "net", buf->p, "bad type for bigpacket: %d", newbuf[0]);

freebigbuf:
	afree(cli->bigrecv.buf);
	cli->bigrecv.buf = NULL;
	cli->bigrecv.size = 0;
	cli->bigrecv.room = 0;
reallyexit:
	pd->UnlockPlayer(buf->p);
	FreeBuffer(buf);
}


void ProcessPresize(Buffer *buf)
{
	ClientData *cli = PPDATA(buf->p, clikey);
	Link *l;
	int size = buf->d.rel.seqnum;

	pd->LockPlayer(buf->p);

	if (cli->sizedrecv.offset == 0)
	{
		/* first packet */
		if (buf->d.rel.data[0] < MAXTYPES)
		{
			cli->sizedrecv.type = buf->d.rel.data[0];
			cli->sizedrecv.totallen = size;
		}
		else
		{
			end_sized(buf->p, 0);
			goto presized_done;
		}
	}

	if (cli->sizedrecv.totallen != size)
	{
		lm->LogP(L_MALICIOUS, "net", buf->p, "Length mismatch in sized packet");
		end_sized(buf->p, 0);
	}
	else if ((cli->sizedrecv.offset + buf->len - 6) > size)
	{
		lm->LogP(L_MALICIOUS, "net", buf->p, "Sized packet overflow");
		end_sized(buf->p, 0);
	}
	else
	{
		for (l = LLGetHead(sizedhandlers + cli->sizedrecv.type); l; l = l->next)
			((SizedPacketFunc)(l->data))
				(buf->p, buf->d.rel.data, buf->len - 6, cli->sizedrecv.offset, size);

		cli->sizedrecv.offset += buf->len - 6;

		if (cli->sizedrecv.offset >= size)
			end_sized(buf->p, 1);
	}

presized_done:
	pd->UnlockPlayer(buf->p);
	FreeBuffer(buf);
}


void ProcessCancel(Buffer *req)
{
	/* the client has request a cancel for the file transfer. that means
	 * we have to go through the outlist and remove all presized
	 * packets. */
	ClientData *cli = PPDATA(req->p, clikey);
	Buffer *buf, *nbuf;
	DQNode *outlist;

	pthread_mutex_lock(&cli->olmtx);
	outlist = &cli->outlist;
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
	pthread_mutex_unlock(&cli->olmtx);

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
void SendRaw(Player *p, byte *data, int len)
{
	byte encbuf[MAXPACKET];
	ClientData *cli = PPDATA(p, clikey);
	Iencrypt *enc = cli->enc;

	memcpy(encbuf, data, len);

#ifdef CFG_DUMP_RAW_PACKETS
	printf("SEND: %d bytes to pid %d\n", len, pid);
	dump_pk(encbuf, len);
#endif

	if (enc)
		len = enc->Encrypt(p, encbuf, len);

	if (len == 0)
		return;

#ifdef CFG_DUMP_RAW_PACKETS
	printf("SEND: %d bytes (after encryption):\n", len);
	dump_pk(encbuf, len);
#endif

	sendto(serversock, encbuf, len, 0,
			(struct sockaddr*)&cli->sin,sizeof(struct sockaddr_in));

	cli->bytessince += len + IP_UDP_OVERHEAD;
	cli->bytesent += len + IP_UDP_OVERHEAD;
	cli->pktsent++;
	global_stats.pktsent++;
}


/* must be called with outlist mutex! */
Buffer * BufferPacket(Player *p, byte *data, int len, int flags,
		RelCallback callback, void *clos)
{
	ClientData *cli = PPDATA(p, clikey);
	Buffer *buf;
	int limit;

	if (!IS_OURS(p)) return NULL;

	assert(len < MAXPACKET);

	/* handle default priority */
	if (GET_PRI(flags) == 0) flags |= NET_PRI_DEFAULT;
	limit = cli->limit * pri_limits[GET_PRI(flags)] / 100;

	/* try the fast path */
	if (flags == NET_PRI_P4 || flags == NET_PRI_P5)
		if (cli->bytessince + len + IP_UDP_OVERHEAD <= limit)
		{
			SendRaw(p, data, len);
			return NULL;
		}

	buf = GetBuffer();
	buf->p = p;
	buf->lastretry = 0;
	buf->pri = GET_PRI(flags);
	buf->callback = callback;
	buf->clos = clos;
	global_stats.pri_stats[buf->pri]++;

	/* get data into packet */
	if (flags & NET_RELIABLE)
	{
		buf->d.rel.t1 = 0x00;
		buf->d.rel.t2 = 0x03;
		memcpy(buf->d.rel.data, data, len);
		buf->len = len + 6;
		buf->d.rel.seqnum = cli->s2cn++;
	}
	else
	{
		memcpy(buf->d.raw, data, len);
		buf->len = len;
	}

	/* add it to out list */
	DQAdd(&cli->outlist, (DQNode*)buf);

	/* if it's urgent, do one retry now */
	if (GET_PRI(flags) > 5)
		if ((cli->bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
		{
			SendRaw(p, buf->d.raw, buf->len);
			buf->lastretry = GTC();
		}

	return buf;
}


void SendToOne(Player *p, byte *data, int len, int flags)
{
	ClientData *cli = PPDATA(p, clikey);
	/* see if we can do it the quick way */
	if (len < MAXPACKET)
	{
		pthread_mutex_lock(&cli->olmtx);
		BufferPacket(p, data, len, flags, NULL, NULL);
		pthread_mutex_unlock(&cli->olmtx);
	}
	else
	{
		/* slight hack */
		Link link = { NULL, p };
		LinkedList lst = { &link, &link };
		SendToSet(&lst, data, len, flags);
	}
}


void SendToArena(Arena *arena, Player *except, byte *data, int len, int flags)
{
	LinkedList set = LL_INITIALIZER;
	Link *link;
	Player *p;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    (p->arena == arena || !arena) &&
		    p != except)
			LLAdd(&set, p);
	pd->Unlock();
	SendToSet(&set, data, len, flags);
	LLEmpty(&set);
}


void SendToTarget(const Target *target, byte *data, int len, int flags)
{
	LinkedList set = LL_INITIALIZER;
	pd->TargetToSet(target, &set);
	SendToSet(&set, data, len, flags);
	LLEmpty(&set);
}


void SendToSet(LinkedList *set, byte *data, int len, int flags)
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
	{
		Link *l;
		for (l = LLGetHead(set); l; l = l->next)
		{
			Player *p = l->data;
			ClientData *cli = PPDATA(p, clikey);
			pthread_mutex_lock(&cli->olmtx);
			BufferPacket(p, data, len, flags, NULL, NULL);
			pthread_mutex_unlock(&cli->olmtx);
		}
	}
}


void SendWithCallback(
		Player *p,
		byte *data,
		int len,
		RelCallback callback,
		void *clos)
{
	ClientData *cli = PPDATA(p, clikey);
	/* we can't handle big packets here */
	assert(len < MAXPACKET);

	pthread_mutex_lock(&cli->olmtx);
	BufferPacket(p, data, len, NET_RELIABLE, callback, clos);
	pthread_mutex_unlock(&cli->olmtx);
}


void SendSized(Player *p, void *clos, int len,
		void (*req)(void *clos, int offset, byte *buf, int needed))
{
	ClientData *cli = PPDATA(p, clikey);
	struct sized_send_data *sd = amalloc(sizeof(*sd));

	sd->request_data = req;
	sd->clos = clos;
	sd->totallen = len;
	sd->offset = 0;

	pthread_mutex_lock(&cli->olmtx);
	LLAdd(&cli->sizedsends, sd);
	pthread_mutex_unlock(&cli->olmtx);
}


i32 GetIP(Player *p)
{
	ClientData *cli = PPDATA(p, clikey);
	return cli->sin.sin_addr.s_addr;
}


void SetLimit(Player *p, int limit)
{
	ClientData *cli = PPDATA(p, clikey);
	cli->limit = limit * CFG_BANDWIDTH_RES / 100;
}


void GetStats(struct net_stats *stats)
{
	if (stats)
		*stats = global_stats;
}

void GetClientStats(Player *p, struct net_client_stats *stats)
{
	ClientData *cli = PPDATA(p, clikey);

	if (!stats || !p) return;

#define ASSIGN(field) stats->field = cli->field
	ASSIGN(s2cn); ASSIGN(c2sn);
	ASSIGN(pktsent); ASSIGN(pktrecvd); ASSIGN(bytesent); ASSIGN(byterecvd);
#undef ASSIGN
	/* encryption */
	if (cli->enc)
		stats->encname = cli->enc->head.name;
	else
		stats->encname = "none";
	/* convert to bytes per second */
	stats->limit = cli->limit * 100 / CFG_BANDWIDTH_RES;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(cli->sin.sin_addr), 16);
	stats->port = cli->sin.sin_port;
}

int GetLastPacketTime(Player *p)
{
	ClientData *cli = PPDATA(p, clikey);
	return GTC() - cli->lastpkt;
}

