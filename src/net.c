
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
#else
#define close(a) closesocket(a)
#endif

#include "asss.h"
#include "encrypt.h"
#include "net-client.h"
#include "protutil.h"


/* defines */

/* debugging option to dump raw packets to the console */
/* #define CFG_DUMP_RAW_PACKETS */

/* whether to increase bandwidth limit only when it's hit */
#define USE_HITLIMIT

/* the size of the ip address hash table. this _must_ be a power of two. */
#define CFG_HASHSIZE 256

/* how many incoming rel packets to buffer for a client */
#define CFG_INCOMING_BUFFER 32


#define MAXTYPES 64

/* ip/udp overhead, in bytes per physical packet */
#define IP_UDP_OVERHEAD 28

/* if we haven't sent a reliable packet after this many tries, drop the
 * connection. */
#define MAXRETRIES 10

/* packets to queue up for sending files */
#define QUEUE_PACKETS 25
/* threshold to start queuing up more packets */
#define QUEUE_THRESHOLD 5

/* we need to know how many packets the client is able to buffer */
#define CLIENT_CAN_BUFFER 30

/* the limits on the bandwidth limit */
#define LOW_LIMIT 1024
#define HIGH_LIMIT 102400
#define CLIP(x, low, high) \
	do { if ((x) > (high)) (x) = (high); else if ((x) < (low)) (x) = (low); } while (0)

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


/* internal flags */
#define NET_ACK 0x0100

/* structs */

#include "packets/reliable.h"

#include "packets/timesync.h"

struct sized_send_data
{
	void (*request_data)(void *clos, int offset, byte *buf, int needed);
	void *clos;
	int totallen, offset;
};

struct Buffer;

typedef struct
{
	/* the player this connection is for, or NULL for a client
	 * connection */
	Player *p;
	/* the client this is a part of, or NULL for a player connection */
	ClientConnection *cc;
	/* the address to send packets to */
	struct sockaddr_in sin;
	/* which of our sockets to use when sending */
	int whichsock;
	/* hash bucket */
	Player *nextinbucket;
	/* sequence numbers for reliable packets */
	i32 s2cn, c2sn;
	/* time of last packet recvd and of initial connection */
	ticks_t lastpkt;
	/* total amounts sent and recvd */
	unsigned long pktsent, pktrecvd;
	unsigned long bytesent, byterecvd;
	/* duplicate reliable packets and reliable retries */
	unsigned long reldups, retries;
	unsigned long pktdropped;
	byte hitmaxretries, hitmaxoutlist, unused1, unused2;
	/* averaged round-trip time and deviation */
	int avgrtt, rttdev;
	/* encryption type */
	Iencrypt *enc;
	/* stuff for recving sized packets, protected by bigmtx */
	struct
	{
		int type;
		int totallen, offset;
	} sizedrecv;
	/* stuff for recving big packets, protected by bigmtx */
	struct
	{
		int size, room;
		byte *buf;
	} bigrecv;
	/* stuff for sending sized packets, protected by olmtx */
	LinkedList sizedsends;
	/* bandwidth control. sincetime is in millis, not ticks */
	ticks_t sincetime;
	int bytessince, limit;
#ifdef USE_HITLIMIT
	int hitlimit;
#endif
	/* the outlist */
	DQNode outlist;
	/* the reliable buffer space */
	struct Buffer *relbuf[CFG_INCOMING_BUFFER];
	/* some mutexes */
	pthread_mutex_t olmtx;
	pthread_mutex_t relmtx;
	pthread_mutex_t bigmtx;
} ConnData;



typedef struct Buffer
{
	DQNode node;
	/* p, len: valid for all buffers */
	/* pri, lastretry: valid for buffers in outlist */
	ConnData *conn;
	short len, pri, retries, flags;
	ticks_t lastretry; /* in millis, not ticks! */
	/* used for reliable buffers in the outlist only { */
	RelCallback callback;
	void *clos;
	/* } */
	union
	{
		struct ReliablePacket rel;
		byte raw[MAXPACKET+4];
	} d;
} Buffer;


typedef struct ListenData
{
	int gamesock, pingsock;
	int port;
	const char *connectas;
} ListenData;


struct ClientConnection
{
	ConnData c;
	Iclientconn *i;
	Iclientencrypt *enc;
	ClientEncryptData *ced;
};


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

local void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len, void *v);
local void AddPacket(int, PacketFunc);
local void RemovePacket(int, PacketFunc);
local void AddSizedPacket(int, SizedPacketFunc);
local void RemoveSizedPacket(int, SizedPacketFunc);
local Player * NewConnection(int type, struct sockaddr_in *, Iencrypt *enc, void *v);
local void GetStats(struct net_stats *stats);
local void GetClientStats(Player *p, struct net_client_stats *stats);
local int GetLastPacketTime(Player *p);
local int GetListenData(unsigned index, int *port, char *connectasbuf, int buflen);

/* net-client interface */
local ClientConnection *MakeClientConnection(const char *addr, int port,
		Iclientconn *icc, Iclientencrypt *ice);
local void SendPacket(ClientConnection *cc, byte *pkt, int len, int flags);
local void DropClientConnection(ClientConnection *cc);

/* internal: */
local inline int HashIP(struct sockaddr_in);
local inline Player * LookupIP(struct sockaddr_in);
local inline void SendRaw(ConnData *, byte *, int);
local void KillConnection(Player *p);
local void ProcessBuffer(Buffer *);
local int InitSockets(void);
local Buffer * GetBuffer(void);
local Buffer * BufferPacket(ConnData *conn, byte *data, int len, int flags,
		RelCallback callback, void *clos);
local void FreeBuffer(Buffer *);

/* threads: */
local void * RecvThread(void *);
local void * SendThread(void *);
local void * RelThread(void *);

local int queue_more_data(void *);

/* network layer header handling: */
local void ProcessKeyResp(Buffer *);
local void ProcessReliable(Buffer *);
local void ProcessGrouped(Buffer *);
local void ProcessSpecial(Buffer *);
local void ProcessAck(Buffer *);
local void ProcessSyncRequest(Buffer *);
local void ProcessBigData(Buffer *);
local void ProcessPresize(Buffer *);
local void ProcessDrop(Buffer *);
local void ProcessCancelReq(Buffer *);
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

local LinkedList listening = LL_INITIALIZER;
local int clientsock;

local LinkedList clientconns = LL_INITIALIZER;
local pthread_mutex_t ccmtx = PTHREAD_MUTEX_INITIALIZER;

local DQNode freelist;
local pthread_mutex_t freemtx;
local pthread_t rel_thread, recv_thread, send_thread;

local int connkey;
local Player * clienthash[CFG_HASHSIZE];
local pthread_mutex_t hashmtx;

/* these are percentages of the total bandwidth that can be used for
 * data of the given priority. this might need tuning. */
local int pri_limits[] =
{
	/* (slot unused)  */ 0,
	/* NET_PRI_N1   = */ 65,
	/* NET_PRI_ZERO = */ 70,
	/* NET_PRI_P1   = */ 75,
	/* NET_PRI_P2   = */ 80,
	/* NET_PRI_P3   = */ 85,
	/* NET_PRI_P4   = */ 95,
	/* NET_PRI_P5   = */ 100,
	/* (reliable)     */ 45,
	/* (acks)         */ 15
};

local struct
{
	int droptimeout;
	int maxoutlist;
} config;

local volatile struct net_stats global_stats;

local void (*oohandlers[])(Buffer*) =
{
	NULL, /* 00 - nothing */
	NULL, /* 01 - key initiation */
	ProcessKeyResp, /* 02 - key response */
	ProcessReliable, /* 03 - reliable */
	ProcessAck, /* 04 - reliable response */
	ProcessSyncRequest, /* 05 - time sync request */
	NULL, /* 06 - time sync response */
	ProcessDrop, /* 07 - close connection */
	ProcessBigData, /* 08 - bigpacket */
	ProcessBigData, /* 09 - bigpacket2 */
	ProcessPresize, /* 0A - presized data (file transfer) */
	ProcessCancelReq, /* 0B - cancel presized */
	ProcessCancel, /* 0C - presized has been cancelled */
	NULL, /* 0D - nothing */
	ProcessGrouped, /* 0E - grouped */
	NULL, /* 0x0F */
	NULL, /* 0x10 */
	NULL, /* 0x11 */
	NULL, /* 0x12 */
	ProcessSpecial, /* 0x13 - cont key response */
	NULL
};

local PacketFunc nethandlers[0x14];


local Inet netint =
{
	INTERFACE_HEAD_INIT(I_NET, "net-udp")

	SendToOne, SendToArena, SendToSet, SendToTarget,
	SendWithCallback, SendSized,

	KillConnection,

	AddPacket, RemovePacket, AddSizedPacket, RemoveSizedPacket,

	ReallyRawSend, NewConnection,

	GetStats, GetClientStats, GetLastPacketTime, GetListenData
};


local Inet_client netclientint =
{
	INTERFACE_HEAD_INIT(I_NET_CLIENT, "net-udp")

	MakeClientConnection,
	SendPacket,
	DropClientConnection
};



/* start of functions */


EXPORT int MM_net(int action, Imodman *mm_, Arena *a)
{
	int i;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		if (!pd || !cfg || !lm || !ml) return MM_FAIL;

		connkey = pd->AllocatePlayerData(sizeof(ConnData));
		if (connkey == -1) return MM_FAIL;

		/* store configuration params */
		/* cfghelp: Net:DropTimeout, global, int, def: 3000
		 * How long to get no data from a client before disconnecting
		 * him (in ticks). */
		config.droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		/* cfghelp: Net:MaxOutlistSize, global, int, def: 200
		 * How many S2C packets the server will buffer for a client
		 * before dropping him. */
		config.maxoutlist = cfg->GetInt(GLOBAL, "Net", "MaxOutlistSize", 200);

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
		pthread_mutex_init(&freemtx, NULL);
		DQInit(&freelist);

		/* start the threads */
		pthread_create(&recv_thread, NULL, RecvThread, NULL);
		pthread_create(&send_thread, NULL, SendThread, NULL);
		pthread_create(&rel_thread, NULL, RelThread, NULL);

		ml->SetTimer(queue_more_data, 20, 11, NULL, NULL);

		/* install ourself */
		mm->RegInterface(&netint, ALLARENAS);
		mm->RegInterface(&netclientint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_PREUNLOAD)
	{
		Link *link;
		Player *p;
		ConnData *conn;

		/* disconnect all clients nicely */
		pd->Lock();
		FOR_EACH_PLAYER_P(p, conn, connkey)
			if (IS_OURS(p))
			{
				byte discon[2] = { 0x00, 0x07 };
				SendRaw(conn, discon, 2);
				if (conn->enc)
				{
					conn->enc->Void(p);
					mm->ReleaseInterface(conn->enc);
					conn->enc = NULL;
				}
			}
		pd->Unlock();

		/* and disconnect our client connections */
		pthread_mutex_lock(&ccmtx);
		for (link = LLGetHead(&clientconns); link; link = link->next)
		{
			ClientConnection *cc = link->data;
			byte pkt[] = { 0x00, 0x07 };
			SendRaw(&cc->c, pkt, sizeof(pkt));
			cc->i->Disconnected();
			if (cc->enc)
			{
				cc->enc->Void(cc->ced);
				mm->ReleaseInterface(cc->enc);
				cc->enc = NULL;
			}
			afree(cc);
		}
		LLEmpty(&clientconns);
		pthread_mutex_unlock(&ccmtx);
	}
	else if (action == MM_UNLOAD)
	{
		Link *link;

		/* uninstall ourself */
		if (mm->UnregInterface(&netint, ALLARENAS))
			return MM_FAIL;

		if (mm->UnregInterface(&netclientint, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(queue_more_data, NULL);

		/* clean up */
		for (i = 0; i < MAXTYPES; i++)
		{
			LLEmpty(handlers + i);
			LLEmpty(sizedhandlers + i);
		}

		/* kill threads */
		pthread_cancel(recv_thread);
		pthread_cancel(send_thread);
		pthread_cancel(rel_thread);
		pthread_join(recv_thread, NULL);
		pthread_join(send_thread, NULL);
		pthread_join(rel_thread, NULL);

		/* close all our sockets */
		for (link = LLGetHead(&listening); link; link = link->next)
		{
			ListenData *ld = link->data;
			close(ld->gamesock);
			close(ld->pingsock);
		}
		LLEnum(&listening, afree);
		LLEmpty(&listening);
		close(clientsock);

		pd->FreePlayerData(connkey);

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
	int b2 = t>>8;
	if (t >= 0 && t < MAXTYPES)
		LLAdd(handlers+t, f);
	else if ((t & 0xff) == 0 && b2 >= 0 &&
	         b2 < (sizeof(nethandlers)/sizeof(nethandlers[0])) &&
	         nethandlers[b2] == NULL)
		nethandlers[b2] = f;
}
void RemovePacket(int t, PacketFunc f)
{
	int b2 = t>>8;
	if (t >= 0 && t < MAXTYPES)
		LLRemove(handlers+t, f);
	else if ((t & 0xff) == 0 && b2 >= 0 &&
	         b2 < (sizeof(nethandlers)/sizeof(nethandlers[0])) &&
	         nethandlers[b2] == f)
		nethandlers[b2] = NULL;
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
		ConnData *conn = PPDATA(p, connkey);
		if (conn->sin.sin_addr.s_addr == sin.sin_addr.s_addr &&
		    conn->sin.sin_port == sin.sin_port)
			break;
		p = conn->nextinbucket;
	}
	pthread_mutex_unlock(&hashmtx);
	return p;
}


local void clear_buffers(Player *p)
{
	ConnData *conn = PPDATA(p, connkey);
	DQNode *outlist = &conn->outlist;
	Buffer *buf, *nbuf;
	Link *l;
	int i;

	pthread_mutex_lock(&conn->olmtx);

	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
	{
		nbuf = (Buffer*)buf->node.next;
		if (buf->callback)
		{
			/* this is ugly, but we have to release the outlist mutex
			 * during these callbacks, because the callback might need
			 * to acquire some mutexes of its own, and we want to avoid
			 * deadlock. */
			pthread_mutex_unlock(&conn->olmtx);
			buf->callback(p, 0, buf->clos);
			pthread_mutex_lock(&conn->olmtx);
		}
		DQRemove((DQNode*)buf);
		FreeBuffer(buf);
	}

	for (l = LLGetHead(&conn->sizedsends); l; l = l->next)
	{
		struct sized_send_data *sd = l->data;
		sd->request_data(sd->clos, 0, NULL, 0);
		afree(sd);
	}
	LLEmpty(&conn->sizedsends);

	pthread_mutex_unlock(&conn->olmtx);

	pthread_mutex_lock(&conn->relmtx);
	for (i = 0; i < CFG_INCOMING_BUFFER; i++)
		if (conn->relbuf[i])
		{
			FreeBuffer(conn->relbuf[i]);
			conn->relbuf[i] = NULL;
		}
	pthread_mutex_unlock(&conn->relmtx);
}


local void InitConnData(ConnData *conn, Iencrypt *enc)
{
	/* set up clientdata */
	memset(conn, 0, sizeof(ConnData));
	pthread_mutex_init(&conn->olmtx, NULL);
	pthread_mutex_init(&conn->relmtx, NULL);
	pthread_mutex_init(&conn->bigmtx, NULL);
	conn->limit = LOW_LIMIT * 3; /* start slow */
#ifdef USE_HITLIMIT
	conn->hitlimit = 0;
#endif
	conn->enc = enc;
	conn->avgrtt = 100; /* an initial guess */
	conn->rttdev = 100;
	conn->bytessince = 0;
	conn->sincetime = current_millis();
	conn->lastpkt = current_ticks();
	LLInit(&conn->sizedsends);
	DQInit(&conn->outlist);
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
	struct sockaddr_in sin;
	int i;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		return -1;
#endif

	/* cfghelp: Net:Listen, global, string
	 * A designation for a port and ip to listen on. Format is one of
	 * 'port', 'port:connectas', or 'ip:port:connectas'. Listen1 through
	 * Listen9 are also supported. A missing or zero-length 'ip' field
	 * means all interfaces. The 'connectas' field can be used to treat
	 * clients differently depending on which port or ip they use to
	 * connect to the server. It serves as a virtual server identifier
	 * for the rest of the server. */
	for (i = 0; i < 10; i++)
	{
		unsigned short port;
		ListenData *ld;
		char secname[] = "Listen#";
		char field1[32], field2[32];
		const char *line, *n;

		secname[6] = i ? i+'0' : 0;
		line = cfg->GetStr(GLOBAL, "Net", secname);

		if (!line) continue;

		ld = amalloc(sizeof(*ld));

		/* set up sin */
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;

		/* figure out what the user has specified */
		n = delimcpy(field1, line, sizeof(field1), ':');
		if (!n)
		{
			/* single field: port */
			sin.sin_addr.s_addr = INADDR_ANY;
			port = (unsigned short)strtol(field1, NULL, 0);
		}
		else
		{
			n = delimcpy(field2, n, sizeof(field2), ':');
			if (!n)
			{
				/* two fields: port, connectas */
				sin.sin_addr.s_addr = INADDR_ANY;
				port = (unsigned short)strtol(field1, NULL, 0);
				ld->connectas = astrdup(field2);
			}
			else
			{
				/* three fields: ip, port, connectas */
				if (inet_aton(field1, &sin.sin_addr) == 0)
					sin.sin_addr.s_addr = INADDR_ANY;
				port = (unsigned short)strtol(field2, NULL, 0);
				ld->connectas = astrdup(n);
			}
		}

		ld->port = port;

		/* now try to get and bind the sockets */
		sin.sin_port = htons(port);

		ld->gamesock = socket(PF_INET, SOCK_DGRAM, 0);
		if (ld->gamesock == -1)
		{
			lm->Log(L_ERROR, "<net> can't create socket for Listen%d", i);
			afree(ld);
			continue;
		}

		if (set_nonblock(ld->gamesock) < 0)
			lm->Log(L_WARN, "<net> can't make socket nonblocking");

		if (bind(ld->gamesock, (struct sockaddr *) &sin, sizeof(sin)) == -1)
		{
			lm->Log(L_ERROR, "<net> can't bind socket to %s:%d for Listen%d",
					inet_ntoa(sin.sin_addr), port, i);
			close(ld->gamesock);
			afree(ld);
			continue;
		}

		sin.sin_port = htons(port + 1);

		ld->pingsock = socket(PF_INET, SOCK_DGRAM, 0);
		if (ld->pingsock == -1)
		{
			lm->Log(L_ERROR, "<net> can't create socket for Listen%d", i);
			close(ld->gamesock);
			afree(ld);
			continue;
		}

		if (set_nonblock(ld->pingsock) < 0)
			lm->Log(L_WARN, "<net> can't make socket nonblocking");

		if (bind(ld->pingsock, (struct sockaddr *) &sin, sizeof(sin)) == -1)
		{
			lm->Log(L_ERROR, "<net> can't bind socket to %s:%d for Listen%d",
					inet_ntoa(sin.sin_addr), port+1, i);
			close(ld->gamesock);
			close(ld->pingsock);
			afree(ld);
			continue;
		}

		LLAdd(&listening, ld);

		lm->Log(L_DRIVEL, "<net> listening on %s:%d (%d)",
				inet_ntoa(sin.sin_addr), port, i);
	}

	/* now grab a socket for the client connections */
	clientsock = socket(PF_INET, SOCK_DGRAM, 0);
	if (clientsock == -1)
		lm->Log(L_ERROR, "<net> can't create socket for client connections");
	else
	{
		if (set_nonblock(clientsock) < 0)
			lm->Log(L_WARN, "<net> can't make socket nonblocking");

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(0);
		sin.sin_addr.s_addr = INADDR_ANY;
		if (bind(clientsock, (struct sockaddr*)&sin, sizeof(sin)) == -1)
			lm->Log(L_ERROR, "<net> can't bind socket for client connections");
	}

	return 0;
}


int GetListenData(unsigned index, int *port, char *connectasbuf, int buflen)
{
	Link *l;
	ListenData *ld;

	/* er, this is going to be quadratic in the common case of iterating
	 * through the list. if it starts getting called from lots of
	 * places, i'll fix it. */
	l = LLGetHead(&listening);
	while (l && index > 0)
		index--, l = l->next;

	if (!l)
		return FALSE;

	ld = l->data;
	if (port)
		*port = ld->port;
	if (connectasbuf)
		astrncpy(connectasbuf, ld->connectas ? ld->connectas : "", buflen);

	return TRUE;
}


#ifdef CFG_DUMP_RAW_PACKETS
local void dump_pk(byte *d, int len)
{
	char str[80];
	int c;
	while (len > 0)
	{
		for (c = 0; c < 16 && len > 0; c++, len--)
			sprintf(str + 3*c, "%02x ", *d++);
		str[strlen(str)-1] = 0;
		puts(str);
	}
}
#endif


local void handle_game_packet(ListenData *ld)
{
	int len, sinsize, status;
	struct sockaddr_in sin;
	Player *p;
	ConnData *conn;

	Buffer *buf;

	buf = GetBuffer();
	sinsize = sizeof(sin);
	len = recvfrom(ld->gamesock, buf->d.raw, MAXPACKET, 0,
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
					(&sin, buf->d.raw, len, ld));
		else if (len > 1)
			lm->Log(L_DRIVEL, "<net> recvd data (%02x %02x ; %d bytes) "
					"before connection established",
					buf->d.raw[0], buf->d.raw[1], len);
		else
			lm->Log(L_DRIVEL, "<net> recvd data (%02x ; %d byte) "
					"before connection established",
					buf->d.raw[0], len);
		goto freebuf;
	}

	/* grab the status */
	status = p->status;
	conn = PPDATA(p, connkey);

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
					(&sin, buf->d.raw, len, ld));
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
		lm->Log(L_WARN, "<net> [pid=%d] packet recieved from bad state %d", p->pid, status);
		goto freebuf;
		/* don't set lastpkt time here */
	}

	buf->conn = conn;
	conn->lastpkt = current_ticks();
	conn->byterecvd += len;
	conn->pktrecvd++;
	global_stats.byterecvd += len;
	global_stats.pktrecvd++;

	/* decrypt the packet */
	{
		Iencrypt *enc = conn->enc;
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

	return;

freebuf:
	FreeBuffer(buf);
}


local void handle_ping_packet(ListenData *ld)
{
	int len, sinsize, dalen = ld->connectas ? strlen(ld->connectas) : 0;
	struct sockaddr_in sin;
	unsigned int data[2];

	sinsize = sizeof(sin);
	len = recvfrom(ld->pingsock, (char*)data, 4, 0,
			(struct sockaddr*)&sin, &sinsize);

	if (len == 4)
	{
		Player *p;
		Link *link;

		data[1] = data[0];
		data[0] = 0;
		pd->Lock();
		FOR_EACH_PLAYER(p)
			if (p->status == S_PLAYING &&
			    p->type != T_FAKE &&
			    p->arena &&
			    ( !ld->connectas ||
			      strncmp(p->arena->name, ld->connectas, dalen) == 0))
				data[0]++;
		pd->Unlock();
		sendto(ld->pingsock, (char*)data, 8, 0,
				(struct sockaddr*)&sin, sinsize);

		global_stats.pcountpings++;
	}
}


local void handle_client_packet(void)
{
	int len, sinsize;
	struct sockaddr_in sin;
	Buffer *buf;
	Link *l;

	buf = GetBuffer();
	sinsize = sizeof(sin);
	memset(&sin, 0, sizeof(sin));
	len = recvfrom(clientsock, buf->d.raw, MAXPACKET, 0,
			(struct sockaddr*)&sin, &sinsize);

	if (len < 1)
	{
		FreeBuffer(buf);
		return;
	}

#ifdef CFG_DUMP_RAW_PACKETS
	printf("RAW CLIENT DATA: %d bytes\n", len);
	dump_pk(buf->d.raw, len);
#endif

	pthread_mutex_lock(&ccmtx);
	for (l = LLGetHead(&clientconns); l; l = l->next)
	{
		ClientConnection *cc = l->data;
		ConnData *conn = &cc->c;
		if (sin.sin_port == conn->sin.sin_port &&
		    sin.sin_addr.s_addr == conn->sin.sin_addr.s_addr)
		{
			Iclientencrypt *enc = cc->enc;

			pthread_mutex_unlock(&ccmtx);

			if (enc)
				len = enc->Decrypt(cc->ced, buf->d.raw, len);
			if (len > 0)
			{
				buf->conn = conn;
				buf->len = len;
				conn->lastpkt = current_ticks();
				conn->byterecvd += len;
				conn->pktrecvd++;
#ifdef CFG_DUMP_RAW_PACKETS
				printf("DECRYPTED CLIENT DATA: %d bytes\n", len);
				dump_pk(buf->d.raw, len);
#endif
				ProcessBuffer(buf);
			}
			else
			{
				FreeBuffer(buf);
				lm->Log(L_MALICIOUS, "<net> (client connection) "
						"failure decrypting packet");
			}
			/* notice this: */
			return;
		}
	}
	pthread_mutex_unlock(&ccmtx);

	FreeBuffer(buf);
	lm->Log(L_WARN, "<net> got data on client port "
			"not from any known connection: %s:%d",
			inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
}


void * RecvThread(void *dummy)
{
	struct timeval tv;
	fd_set myfds, selfds;
	int maxfd = 5;
	Link *l;

	/* set up the fd set we'll be using */
	FD_ZERO(&myfds);
	for (l = LLGetHead(&listening); l; l = l->next)
	{
		ListenData *ld = l->data;
		FD_SET(ld->pingsock, &myfds);
		if (ld->pingsock > maxfd)
			maxfd = ld->pingsock;
		FD_SET(ld->gamesock, &myfds);
		if (ld->gamesock > maxfd)
			maxfd = ld->gamesock;
	}
	if (clientsock >= 0)
	{
		FD_SET(clientsock, &myfds);
		if (clientsock > maxfd)
			maxfd = clientsock;
	}

	for (;;)
	{
		/* wait for a packet */
		do {
			pthread_testcancel();
			selfds = myfds;
			tv.tv_sec = 10;
			tv.tv_usec = 0;
		} while (select(maxfd+1, &selfds, NULL, NULL, &tv) < 1);

		/* process whatever we got */
		for (l = LLGetHead(&listening); l; l = l->next)
		{
			ListenData *ld = l->data;
			if (FD_ISSET(ld->gamesock, &selfds))
				handle_game_packet(ld);

			if (FD_ISSET(ld->pingsock, &selfds))
				handle_ping_packet(ld);

		}
		if (clientsock >= 0 && FD_ISSET(clientsock, &selfds))
			handle_client_packet();
	}
	return NULL;
}


local void submit_rel_stats(Player *p)
{
	if (lagc)
	{
		ConnData *conn = PPDATA(p, connkey);
		struct ReliableLagData rld;
		rld.reldups = conn->reldups;
		rld.c2sn = conn->c2sn;
		rld.retries = conn->retries;
		rld.s2cn = conn->s2cn;
		lagc->RelStats(p, &rld);
	}
}


local void end_sized(Player *p, int success)
{
	ConnData *conn = PPDATA(p, connkey);
	if (conn->sizedrecv.offset != 0)
	{
		Link *l;
		u8 type = conn->sizedrecv.type;
		int arg = success ? conn->sizedrecv.totallen : -1;
		/* tell listeners that they're cancelled */
		if (type < MAXTYPES)
			for (l = LLGetHead(sizedhandlers + type); l; l = l->next)
				((SizedPacketFunc)(l->data))(p, NULL, 0, arg, arg);
		conn->sizedrecv.type = 0;
		conn->sizedrecv.totallen = 0;
		conn->sizedrecv.offset = 0;
	}
}


local Player *get_next_player(int *pos)
{
	Link *l;
	int i = 0;

	pd->Lock();
	for (l = LLGetHead(&pd->playerlist); l; l = l->next)
	{
		Player *cp = l->data;
		if (!IS_OURS(cp))
			continue;
		else if (i++ == *pos)
			break;
	}
	pd->Unlock();

	if (l)
	{
		(*pos)++;
		return l->data;
	}
	else
	{
		*pos = 0;
		return NULL;
	}
}


int queue_more_data(void *dummy)
{
#define REQUESTATONCE (QUEUE_PACKETS*480)
	static int nextplayer = 0;

	byte buffer[REQUESTATONCE], *dp;
	struct ReliablePacket packet;
	int needed;
	Link *l;
	Player *p;
	ConnData *conn;

	p = get_next_player(&nextplayer);
	conn = PPDATA(p, connkey);

	if (p &&
	    p->status < S_TIMEWAIT &&
	    pthread_mutex_trylock(&conn->olmtx) == 0)
	{
		if ((l = LLGetHead(&conn->sizedsends)) &&
		    DQCount(&conn->outlist) < QUEUE_THRESHOLD)
		{
			struct sized_send_data *sd = l->data;

			/* unlock while we get the data */
			pthread_mutex_unlock(&conn->olmtx);

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
			pthread_mutex_lock(&conn->olmtx);

			/* put data in outlist, in 480 byte chunks */
			dp = buffer;
			while (needed > 480)
			{
				memcpy(packet.data, dp, 480);
				BufferPacket(conn, (byte*)&packet, 486, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);
				dp += 480;
				needed -= 480;
			}
			if (needed > 0)
			{
				memcpy(packet.data, dp, needed);
				BufferPacket(conn, (byte*)&packet, needed + 6, NET_PRI_N1 | NET_RELIABLE, NULL, NULL);
			}

			/* check if we need more */
			if (sd->offset >= sd->totallen)
			{
				/* notify sender that this is the end */
				sd->request_data(sd->clos, sd->offset, NULL, 0);
				LLRemove(&conn->sizedsends, sd);
				afree(sd);
			}

		}
		pthread_mutex_unlock(&conn->olmtx);
	}

	return TRUE;
}


/* call with outlistmtx locked */
local void send_outgoing(ConnData *conn)
{
	byte gbuf[MAXPACKET] = { 0x00, 0x0E };
	byte *gptr = gbuf + 2;

	ticks_t now = current_millis();
	int gcount = 0, bytessince, pri, retries = 0, minseqnum;
	int outlistlen = 0;
	Buffer *buf, *nbuf;
	DQNode *outlist;
	/* use an estimate of the average round-trip time to figure out when
	 * to resend a packet */
	unsigned long timeout = conn->avgrtt + 4*conn->rttdev;

	CLIP(timeout, 10, 2000);

	/* adjust the current idea of how many bytes have been sent in the
	 * last second */
	if (TICK_DIFF(now, conn->sincetime) > (1000/7))
	{
		conn->bytessince = conn->bytessince * 7 / 8;
		conn->sincetime = TICK_MAKE(conn->sincetime + (1000/7));
	}

	/* we keep a local copy of bytessince to account for the
	 * sizes of stuff in grouped packets. */
	bytessince = conn->bytessince;

	outlist = &conn->outlist;

	/* find smallest seqnum remaining in outlist */
	minseqnum = INT_MAX;
	for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = (Buffer*)buf->node.next)
		if (IS_REL(buf) &&
		    buf->d.rel.seqnum < minseqnum)
			minseqnum = buf->d.rel.seqnum;

	/* process highest priority first */
	for (pri = 9; pri > 0; pri--)
	{
		int limit = bytessince + (conn->limit - bytessince) * pri_limits[pri] / 100;

		for (buf = (Buffer*)outlist->next; (DQNode*)buf != outlist; buf = nbuf)
		{
			nbuf = (Buffer*)buf->node.next;

			if (buf->pri != pri)
				continue;

			outlistlen++;

			/* check if it's time to send this yet (increase timeout
			 * exponentially). */
			if (buf->retries != 0 &&
			    (unsigned long)TICK_DIFF(now, buf->lastretry) <= (timeout << (buf->retries-1)))
				continue;

			/* only buffer fixed number of rel packets to client */
			if (IS_REL(buf) && (buf->d.rel.seqnum - minseqnum) > CLIENT_CAN_BUFFER)
				continue;

			if ((bytessince + buf->len + IP_UDP_OVERHEAD) > limit)
			{
				/* try dropping it, if we can */
				if (buf->flags & NET_DROPPABLE)
				{
					DQRemove((DQNode*)buf);
					FreeBuffer(buf);
					conn->pktdropped++;
				}
				/* but in either case, skip it */
#ifdef USE_HITLIMIT
				conn->hitlimit = 1;
#endif
				continue;
			}

			if (buf->retries >= MAXRETRIES)
			{
				/* we've already tried enough, just give up */
				conn->hitmaxretries = 1;
				return;
			}
			else if (buf->retries > 0)
			{
				/* this is a retry, not an initial send. record it
				 * for lag stats and also halve bw limit (with
				 * clipping) */
				retries++;
				conn->limit /= 2;
				CLIP(conn->limit, LOW_LIMIT, HIGH_LIMIT);
			}

			buf->lastretry = now;
			buf->retries++;

			/* try to group it */
			if (buf->len <= 255 &&
			    buf->callback == NULL &&
			    ((gptr - gbuf) + buf->len) < (MAXPACKET-10))
			{
				*gptr++ = (unsigned char)buf->len;
				memcpy(gptr, buf->d.raw, buf->len);
				gptr += buf->len;
				bytessince += buf->len + 1;
				gcount++;
				if (!IS_REL(buf))
				{
					DQRemove((DQNode*)buf);
					FreeBuffer(buf);
				}
			}
			else
			{
				/* send immediately */
				bytessince += buf->len + IP_UDP_OVERHEAD;
				SendRaw(conn, buf->d.raw, buf->len);
				if (!IS_REL(buf))
				{
					/* if we just sent an unreliable packet,
					 * free it so we don't send it again. */
					DQRemove((DQNode*)buf);
					FreeBuffer(buf);
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
		SendRaw(conn, gbuf + 3, (gptr - gbuf) - 3);
	}
	else if (gcount > 1)
	{
		/* send the whole thing as a group */
		SendRaw(conn, gbuf, gptr - gbuf);
	}

	conn->retries += retries;

	if (outlistlen > config.maxoutlist)
		conn->hitmaxoutlist = 1;
}


/* call with player status locked */
local void process_lagouts(Player *p, unsigned int gtc, LinkedList *tokill, LinkedList *tofree)
{
	ConnData *conn = PPDATA(p, connkey);
	/* this is used for lagouts and also for timewait */
	int diff = TICK_DIFF(gtc, conn->lastpkt);

	/* process lagouts */
	if (p->whenloggedin == 0 && /* acts as flag to prevent dups */
	    p->status < S_LEAVING_ZONE && /* don't kick them if they're already on the way out */
	    (diff > config.droptimeout || conn->hitmaxretries || conn->hitmaxoutlist))
	{
		Ichat *chat= mm->GetInterface(I_CHAT, ALLARENAS);
		if (chat)
			chat->SendMessage(p, "You have been disconnected because the server has not "
					"been receiving data from you.");
		mm->ReleaseInterface(chat);

		if (conn->hitmaxretries)
			lm->Log(L_INFO, "<net> [%s] [pid=%d] player kicked for too many reliable retries",
					p->name, p->pid);
		else if (conn->hitmaxoutlist)
			lm->Log(L_INFO, "<net> [%s] [pid=%d] player kicked for too many pkts in outlist",
					p->name, p->pid);
		else
			lm->Log(L_INFO, "<net> [%s] [pid=%d] player kicked for no data",
					p->name, p->pid);

		LLAdd(tokill, p);
	}

	/* process timewait state. */
	/* status is locked in here. */
	if (p->status == S_TIMEWAIT)
	{
		char drop[2] = {0x00, 0x07};
		int bucket;

		/* here, send disconnection packet */
		SendToOne(p, drop, 2, NET_PRI_P5);

		/* tell encryption to forget about him */
		if (conn->enc)
		{
			conn->enc->Void(p);
			mm->ReleaseInterface(conn->enc);
			conn->enc = NULL;
		}

		/* log message */
		lm->Log(L_INFO, "<net> [%s] [pid=%d] disconnected", p->name, p->pid);

		pthread_mutex_lock(&hashmtx);
		bucket = HashIP(conn->sin);
		if (clienthash[bucket] == p)
			clienthash[bucket] = conn->nextinbucket;
		else
		{
			Player *j = clienthash[bucket];
			ConnData *jcli = PPDATA(j, connkey);

			while (j && jcli->nextinbucket != p)
			{
				j = jcli->nextinbucket;
				jcli = PPDATA(j, connkey);
			}
			if (j)
				jcli->nextinbucket = conn->nextinbucket;
			else
			{
				lm->Log(L_ERROR, "<net> internal error: "
						"established connection not in hash table");
			}
		}
		pthread_mutex_unlock(&hashmtx);

		LLAdd(tofree, p);
	}
}


void * SendThread(void *dummy)
{
	for (;;)
	{
		ticks_t gtc;
		ConnData *conn;
		Player *p;
		Link *link;
		ClientConnection *dropme;
		LinkedList tofree = LL_INITIALIZER;
		LinkedList tokill = LL_INITIALIZER;

		/* first send outgoing packets (players) */
		pd->Lock();
		FOR_EACH_PLAYER_P(p, conn, connkey)
			if (p->status < S_TIMEWAIT &&
			    IS_OURS(p) &&
			    pthread_mutex_trylock(&conn->olmtx) == 0)
			{
				send_outgoing(conn);
				submit_rel_stats(p);
				pthread_mutex_unlock(&conn->olmtx);
			}
		pd->Unlock();

		/* process lagouts and timewait */
		pd->Lock();
		gtc = current_ticks();
		FOR_EACH_PLAYER(p)
			if (IS_OURS(p))
				process_lagouts(p, gtc, &tokill, &tofree);
		pd->Unlock();

		/* now kill the ones we needed to above */
		for (link = LLGetHead(&tokill); link; link = link->next)
			KillConnection(link->data);
		LLEmpty(&tokill);

		/* and free ... */
		for (link = LLGetHead(&tofree); link; link = link->next)
		{
			Player *p = link->data;
			ConnData *conn = PPDATA(p, connkey);
			/* one more time, just to be sure */
			clear_buffers(p);
			pthread_mutex_destroy(&conn->olmtx);
			pd->FreePlayer(link->data);
		}
		LLEmpty(&tofree);

		/* outgoing packets and lagouts for client connections */
		dropme = NULL;
		pthread_mutex_lock(&ccmtx);
		gtc = current_ticks();
		for (link = LLGetHead(&clientconns); link; link = link->next)
		{
			ClientConnection *cc = link->data;
			pthread_mutex_lock(&cc->c.olmtx);
			send_outgoing(&cc->c);
			pthread_mutex_unlock(&cc->c.olmtx);
			/* we can't drop it in the loop because we're holding ccmtx.
			 * keep track of it and drop it later. FIXME: there are
			 * still all sorts of race conditions relating to ccs. as
			 * long as nobody uses DropClientConnection from outside of
			 * net, we're safe for now, but they should be cleaned up. */
			if (cc->c.hitmaxretries ||
			    /* use a special limit of 65 seconds here, unless we
			     * haven't gotten _any_ packets, then use 10. */
			    TICK_DIFF(gtc, cc->c.lastpkt) > (cc->c.pktrecvd ? 6500 : 1000))
				dropme = cc;
		}
		pthread_mutex_unlock(&ccmtx);

		if (dropme)
		{
			dropme->i->Disconnected();
			DropClientConnection(dropme);
		}

		pthread_testcancel();
		usleep(10000); /* 1/100 second */
		pthread_testcancel();
	}
	return NULL;
}


void * RelThread(void *dummy)
{
	int pos = 0;
	for (;;)
	{
		Player *p = get_next_player(&pos);
		ConnData *conn = PPDATA(p, connkey);

		if (p &&
		    p->status < S_TIMEWAIT &&
		    pthread_mutex_trylock(&conn->relmtx) == 0)
		{
			int spot = conn->c2sn % CFG_INCOMING_BUFFER;

			if (conn->relbuf[spot])
			{
				Buffer *buf = conn->relbuf[spot];

				conn->c2sn++;
				conn->relbuf[spot] = NULL;

				/* don't hold mutex while processing packet */
				pthread_mutex_unlock(&conn->relmtx);

				/* process it */
				buf->len -= 6;
				memmove(buf->d.raw, buf->d.rel.data, buf->len);
				ProcessBuffer(buf);
			}
			else
				pthread_mutex_unlock(&conn->relmtx);
		}

		pthread_testcancel();
		usleep(10000); /* 1/100 second */
		pthread_testcancel();
	}
}


/* ProcessBuffer
 * unreliable packets will be processed before the call returns and freed.
 * network packets will be processed by the appropriate network handler,
 * which may free it or not.
 */
void ProcessBuffer(Buffer *buf)
{
	ConnData *conn = buf->conn;
	if (buf->d.rel.t1 == 0x00)
	{
		if (buf->d.rel.t2 < (sizeof(oohandlers)/sizeof(*oohandlers)) &&
				oohandlers[(unsigned)buf->d.rel.t2])
			(oohandlers[(unsigned)buf->d.rel.t2])(buf);
		else
		{
			if (conn->p)
				lm->Log(L_MALICIOUS, "<net> [%s] [pid=%d] unknown network subtype %d",
						conn->p->name, conn->p->pid, buf->d.rel.t2);
			else
				lm->Log(L_MALICIOUS, "<net> (client connection) unknown network subtype %d",
						buf->d.rel.t2);
			FreeBuffer(buf);
		}
	}
	else if (buf->d.rel.t1 < MAXTYPES)
	{
		if (conn->p)
		{
			LinkedList *lst = handlers + (int)buf->d.rel.t1;
			Link *l;

			pd->LockPlayer(conn->p);
			for (l = LLGetHead(lst); l; l = l->next)
				((PacketFunc)(l->data))(conn->p, buf->d.raw, buf->len);
			pd->UnlockPlayer(conn->p);
		}
		else if (conn->cc)
			conn->cc->i->HandlePacket(buf->d.raw, buf->len);

		FreeBuffer(buf);
	}
	else
	{
		if (conn->p)
			lm->Log(L_MALICIOUS, "<net> [%s] [pid=%d] unknown packet type %d",
					conn->p->name, conn->p->pid, buf->d.rel.t1);
		else
			lm->Log(L_MALICIOUS, "<net> (client connection) unknown packet type %d",
					buf->d.rel.t1);
		FreeBuffer(buf);
	}
}


Player * NewConnection(int type, struct sockaddr_in *sin, Iencrypt *enc, void *v_ld)
{
	int bucket;
	Player *p;
	ConnData *conn;
	ListenData *ld = v_ld;

	/* try to find this sin in the hash table */
	if (sin && (p = LookupIP(*sin)))
	{
		/* we found it. if its status is S_CONNECTED, just return the
		 * pid. it means we have to redo part of the connection init. */

		/* whether we return p or not, we still have to drop the
		 * reference to enc that we were given. */
		mm->ReleaseInterface(enc);

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
	conn = PPDATA(p, connkey);

	InitConnData(conn, enc);

	conn->p = p;

	/* copy info from ListenData */
	conn->whichsock = ld->gamesock;
	p->connectas = ld->connectas;

	/* set ip address in player struct */
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(p->ipaddr, inet_ntoa(sin->sin_addr), sizeof(p->ipaddr));

	/* add him to his hash bucket */
	pthread_mutex_lock(&hashmtx);
	if (sin)
	{
		memcpy(&conn->sin, sin, sizeof(struct sockaddr_in));
		bucket = HashIP(*sin);
		conn->nextinbucket = clienthash[bucket];
		clienthash[bucket] = p;
	}
	else
	{
		conn->nextinbucket = NULL;
	}
	pthread_mutex_unlock(&hashmtx);

	if (sin)
		lm->Log(L_DRIVEL,"<net> [pid=%d] new connection from %s:%i",
				p->pid, inet_ntoa(sin->sin_addr), ntohs(sin->sin_port));
	else
		lm->Log(L_DRIVEL,"<net> [pid=%d] new internal connection", p->pid);

	return p;
}


void KillConnection(Player *p)
{
	pd->LockPlayer(p);

	/* check to see if he has any ongoing file transfers */
	end_sized(p, 0);

	/* this will set status to at least S_LEAVING_ARENA, if the player
	 * was in anything above S_LOGGEDIN. */
	if (p->arena)
	{
		Iarenaman *aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		if (aman) aman->LeaveArena(p);
		mm->ReleaseInterface(aman);
	}

	pd->WriteLock();

	/* make sure that he's on his way out, in case he was kicked before
	 * fully logging in. */
	if (p->status < S_LEAVING_ARENA)
		p->status = S_LEAVING_ZONE;

	/* set this special flag so that the player will be set to leave
	 * the zone when the S_LEAVING_ARENA-initiated actions are
	 * completed. */
	p->whenloggedin = S_LEAVING_ZONE;

	pd->WriteUnlock();
	pd->UnlockPlayer(p);

	/* remove outgoing packets from the queue. this partially eliminates
	 * the need for a timewait state. */
	clear_buffers(p);
}


void ProcessKeyResp(Buffer *buf)
{
	ConnData *conn = buf->conn;

	if (buf->len != 6)
	{
		FreeBuffer(buf);
		return;
	}

	if (conn->cc)
		conn->cc->i->Connected();
	else if (conn->p)
		lm->LogP(L_MALICIOUS, "net", conn->p, "got key response packet");
	FreeBuffer(buf);
}


void ProcessReliable(Buffer *buf)
{
	int sn = buf->d.rel.seqnum;
	ConnData *conn = buf->conn;

	if (buf->len < 7)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->relmtx);

	if ((sn - conn->c2sn) >= CFG_INCOMING_BUFFER)
	{
		/* just drop it */
		pthread_mutex_unlock(&conn->relmtx);
		if (conn->p)
			lm->Log(L_DRIVEL, "<net> [%s] [pid=%d] reliable packet "
					"with too big delta (%d - %d)",
					conn->p->name, conn->p->pid, sn, conn->c2sn);
		else
			lm->Log(L_DRIVEL, "<net> (client connection) reliable packet "
					"with too big delta (%d - %d)",
					sn, conn->c2sn);
		FreeBuffer(buf);
	}
	else
	{
		/* ack and store it */
		struct
		{
			u8 t0, t1;
			i32 seqnum;
		} ack = { 0x00, 0x04, sn };

		/* add to rel stuff to be processed */
		int spot = buf->d.rel.seqnum % CFG_INCOMING_BUFFER;
		if ((sn < conn->c2sn) || conn->relbuf[spot])
		{
			/* a dup */
			conn->reldups++;
			FreeBuffer(buf);
		}
		else
			conn->relbuf[spot] = buf;
		pthread_mutex_unlock(&conn->relmtx);

		/* send the ack */
		pthread_mutex_lock(&conn->olmtx);
		BufferPacket(conn, (byte*)&ack, sizeof(ack), NET_ACK, NULL, NULL);
		pthread_mutex_unlock(&conn->olmtx);
	}
}


void ProcessGrouped(Buffer *buf)
{
	int pos = 2, len = 1;

	if (buf->len < 4)
	{
		FreeBuffer(buf);
		return;
	}

	while (pos < buf->len && len > 0)
	{
		len = buf->d.raw[pos++];
		if (pos + len <= buf->len)
		{
			Buffer *b = GetBuffer();
			b->conn = buf->conn;
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
	ConnData *conn = buf->conn;
	Buffer *b, *nbuf;
	DQNode *outlist;

	if (buf->len != 6)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->olmtx);
	outlist = &conn->outlist;
	for (b = (Buffer*)outlist->next; (DQNode*)b != outlist; b = nbuf)
	{
		nbuf = (Buffer*)b->node.next;
		if (IS_REL(b) &&
		    b->d.rel.seqnum == buf->d.rel.seqnum)
		{
			DQRemove((DQNode*)b);
			pthread_mutex_unlock(&conn->olmtx);

			if (b->callback)
				b->callback(conn->p, 1, b->clos);

			if (b->retries == 1)
			{
				int rtt = TICK_DIFF(current_millis(), b->lastretry);
				int dev = conn->avgrtt - rtt;
				if (dev < 0) dev = -dev;
				conn->rttdev = (conn->rttdev * 3 + dev) / 4;
				conn->avgrtt = (conn->avgrtt * 7 + rtt) / 8;
				if (lagc && conn->p) lagc->RelDelay(conn->p, rtt);
			}

			/* handle limit adjustment */
#ifdef USE_HITLIMIT
			if (conn->hitlimit)
			{
				if (conn->p && conn->p->status != S_PLAYING)
					conn->limit += 2048*2048/conn->limit;
				else
					conn->limit += 1024*1024/conn->limit;
				conn->hitlimit = 0;
			}
			else
#endif
				conn->limit += 540*540/conn->limit;

			CLIP(conn->limit, LOW_LIMIT, HIGH_LIMIT);

			FreeBuffer(b);
			FreeBuffer(buf);
			return;
		}
	}
	pthread_mutex_unlock(&conn->olmtx);
	FreeBuffer(buf);
}


void ProcessSyncRequest(Buffer *buf)
{
	ConnData *conn = buf->conn;
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)(buf->d.raw);
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time };

	if (buf->len != 14)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->olmtx);
	ts.servertime = current_ticks();
	/* note: this bypasses bandwidth limits */
	SendRaw(conn, (byte*)&ts, sizeof(ts));
	pthread_mutex_unlock(&conn->olmtx);

	/* submit data to lagdata */
	if (lagc && conn->p)
	{
		struct ClientPLossData data;
		data.s_pktrcvd = conn->pktrecvd;
		data.s_pktsent = conn->pktsent;
		data.c_pktrcvd = cts->pktrecvd;
		data.c_pktsent = cts->pktsent;
		lagc->ClientPLoss(conn->p, &data);
	}

	FreeBuffer(buf);
}


void ProcessDrop(Buffer *buf)
{
	if (buf->len != 2)
	{
		FreeBuffer(buf);
		return;
	}

	if (buf->conn->p)
		KillConnection(buf->conn->p);
	else if (buf->conn->cc)
	{
		buf->conn->cc->i->Disconnected();
		/* FIXME: this sends an extra 0007 to the client. that should
		 * probably go away. */
		DropClientConnection(buf->conn->cc);
	}
	FreeBuffer(buf);
}


void ProcessBigData(Buffer *buf)
{
	ConnData *conn = buf->conn;
	int newsize;
	byte *newbuf;

	if (buf->len < 3)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->bigmtx);

	newsize = conn->bigrecv.size + buf->len - 2;

	if (newsize <= 0 || newsize > MAXBIGPACKET)
	{
		if (conn->p)
			lm->LogP(L_MALICIOUS, "net", conn->p,
					"refusing to allocate %d bytes (> %d)", newsize, MAXBIGPACKET);
		else
			lm->Log(L_MALICIOUS, "<net> (client connection) "
					"refusing to allocate %d bytes (> %d)", newsize, MAXBIGPACKET);
		goto freebigbuf;
	}

	if (conn->bigrecv.room < newsize)
	{
		conn->bigrecv.room *= 2;
		if (conn->bigrecv.room < newsize)
			conn->bigrecv.room = newsize;
		newbuf = realloc(conn->bigrecv.buf, conn->bigrecv.room);
		if (newbuf)
			conn->bigrecv.buf = newbuf;
	}
	else
		newbuf = conn->bigrecv.buf;

	if (!newbuf)
	{
		if (conn->p)
			lm->LogP(L_ERROR, "net", conn->p, "cannot allocate %d bytes "
					"for bigpacket", newsize);
		else
			lm->Log(L_ERROR, "<net> (client connection) cannot allocate %d bytes "
					"for bigpacket", newsize);
		goto freebigbuf;
	}

	memcpy(newbuf + conn->bigrecv.size, buf->d.raw + 2, buf->len - 2);

	conn->bigrecv.buf = newbuf;
	conn->bigrecv.size = newsize;

	if (buf->d.rel.t2 == 0x08) goto reallyexit;

	if (newbuf[0] > 0 && newbuf[0] < MAXTYPES)
	{
		if (conn->p)
		{
			LinkedList *lst = handlers + (int)newbuf[0];
			Link *l;
			pd->LockPlayer(conn->p);
			for (l = LLGetHead(lst); l; l = l->next)
				((PacketFunc)(l->data))(conn->p, newbuf, newsize);
			pd->UnlockPlayer(conn->p);
		}
		else
			conn->cc->i->HandlePacket(newbuf, newsize);
	}
	else
	{
		if (conn->p)
			lm->LogP(L_WARN, "net", conn->p, "bad type for bigpacket: %d", newbuf[0]);
		else
			lm->Log(L_WARN, "<net> (client connection) bad type for bigpacket: %d", newbuf[0]);
	}

freebigbuf:
	afree(conn->bigrecv.buf);
	conn->bigrecv.buf = NULL;
	conn->bigrecv.size = 0;
	conn->bigrecv.room = 0;
reallyexit:
	pthread_mutex_unlock(&conn->bigmtx);
	FreeBuffer(buf);
}


void ProcessPresize(Buffer *buf)
{
	ConnData *conn = buf->conn;
	Link *l;
	int size = buf->d.rel.seqnum;

	if (buf->len < 7)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->bigmtx);

	/* only handle presized packets for player connections, not client
	 * connections. */
	if (!conn->p)
		goto presized_done;

	if (conn->sizedrecv.offset == 0)
	{
		/* first packet */
		if (buf->d.rel.data[0] < MAXTYPES)
		{
			conn->sizedrecv.type = buf->d.rel.data[0];
			conn->sizedrecv.totallen = size;
		}
		else
		{
			end_sized(conn->p, 0);
			goto presized_done;
		}
	}

	if (conn->sizedrecv.totallen != size)
	{
		lm->LogP(L_MALICIOUS, "net", conn->p, "length mismatch in sized packet");
		end_sized(conn->p, 0);
	}
	else if ((conn->sizedrecv.offset + buf->len - 6) > size)
	{
		lm->LogP(L_MALICIOUS, "net", conn->p, "sized packet overflow");
		end_sized(conn->p, 0);
	}
	else
	{
		for (l = LLGetHead(sizedhandlers + conn->sizedrecv.type); l; l = l->next)
			((SizedPacketFunc)(l->data))
				(conn->p, buf->d.rel.data, buf->len - 6, conn->sizedrecv.offset, size);

		conn->sizedrecv.offset += buf->len - 6;

		if (conn->sizedrecv.offset >= size)
			end_sized(conn->p, 1);
	}

presized_done:
	pthread_mutex_unlock(&conn->bigmtx);
	FreeBuffer(buf);
}


void ProcessCancelReq(Buffer *buf)
{
	/* the client has requested a cancel for the file transfer */
	byte pkt[] = {0x00, 0x0C};
	ConnData *conn = buf->conn;
	struct sized_send_data *sd;
	Link *l;

	if (buf->len != 2)
	{
		FreeBuffer(buf);
		return;
	}

	pthread_mutex_lock(&conn->olmtx);

	/* cancel current presized transfer */
	if ((l = LLGetHead(&conn->sizedsends)) && (sd = l->data))
	{
		sd->request_data(sd->clos, 0, NULL, 0);
		afree(sd);
	}
	LLRemoveFirst(&conn->sizedsends);

	BufferPacket(conn, pkt, sizeof(pkt), NET_RELIABLE, NULL, NULL);

	pthread_mutex_unlock(&conn->olmtx);

	FreeBuffer(buf);
}


void ProcessCancel(Buffer *req)
{
	if (req->len != 2)
	{
		FreeBuffer(req);
		return;
	}

	/* the client is cancelling its current file transfer */
	if (req->conn->p)
		end_sized(req->conn->p, 0);
	FreeBuffer(req);
}


/* passes packet to the appropriate nethandlers function */
void ProcessSpecial(Buffer *buf)
{
	if (nethandlers[(unsigned)buf->d.rel.t2] && buf->conn->p)
		nethandlers[(unsigned)buf->d.rel.t2](buf->conn->p, buf->d.raw, buf->len);
	FreeBuffer(buf);
}


void ReallyRawSend(struct sockaddr_in *sin, byte *pkt, int len, void *v_ld)
{
	ListenData *ld = v_ld;
#ifdef CFG_DUMP_RAW_PACKETS
	printf("SENDRAW: %d bytes\n", len);
	dump_pk(pkt, len);
#endif
	sendto(ld->gamesock, pkt, len, 0,
			(struct sockaddr*)sin, sizeof(struct sockaddr_in));
}


/* IMPORTANT: anyone calling SendRaw MUST hold the outlistmtx for the
 * player that they're sending data to if you want bytessince to be
 * accurate. */
void SendRaw(ConnData *conn, byte *data, int len)
{
	byte encbuf[MAXPACKET];
	Player *p = conn->p;

	memcpy(encbuf, data, len);

#ifdef CFG_DUMP_RAW_PACKETS
	printf("SEND: %d bytes to pid %d\n", len, p ? p->pid : -1);
	dump_pk(encbuf, len);
#endif

	if (conn->enc && p)
		len = conn->enc->Encrypt(p, encbuf, len);
	else if (conn->cc && conn->cc->enc)
		len = conn->cc->enc->Encrypt(conn->cc->ced, encbuf, len);

	if (len == 0)
		return;

#ifdef CFG_DUMP_RAW_PACKETS
	printf("SEND: %d bytes (after encryption):\n", len);
	dump_pk(encbuf, len);
#endif

	sendto(conn->whichsock, encbuf, len, 0,
			(struct sockaddr*)&conn->sin,sizeof(struct sockaddr_in));

	conn->bytessince += len + IP_UDP_OVERHEAD;
	conn->bytesent += len;
	conn->pktsent++;
	global_stats.bytesent += len;
	global_stats.pktsent++;
}


/* must be called with outlist mutex! */
Buffer * BufferPacket(ConnData *conn, byte *data, int len, int flags,
		RelCallback callback, void *clos)
{
	Buffer *buf;
	int limit;

	assert(len <= MAXPACKET);

	/* handle default priority */
	if (GET_PRI(flags) == 0) flags |= NET_PRI_DEFAULT;

	limit = conn->bytessince + (conn->limit - conn->bytessince) * pri_limits[GET_PRI(flags)] / 100;

	/* try the fast path */
	if (GET_PRI(flags) > 5 && (flags & NET_RELIABLE) == 0)
	{
		if ((int)conn->bytessince + len + IP_UDP_OVERHEAD <= limit)
		{
			SendRaw(conn, data, len);
			return NULL;
		}
		else
		{
#ifdef USE_HITLIMIT
			conn->hitlimit = 1;
#endif
			if (flags & NET_DROPPABLE)
			{
				conn->pktdropped++;
				return NULL;
			}
		}
	}

	buf = GetBuffer();
	buf->conn = conn;
	buf->lastretry = TICK_MAKE(current_millis() - 10000U);
	buf->retries = 0;
	buf->pri = GET_PRI(flags);
	buf->callback = callback;
	buf->clos = clos;
	buf->flags = flags;

	/* get data into packet */
	if (flags & NET_RELIABLE)
	{
		buf->d.rel.t1 = 0x00;
		buf->d.rel.t2 = 0x03;
		memcpy(buf->d.rel.data, data, len);
		buf->len = len + 6;
		buf->d.rel.seqnum = conn->s2cn++;
		/* rel packets get a special priority to handle bandwidth
		 * reservation */
		buf->pri = 8;
	}
	else
	{
		memcpy(buf->d.raw, data, len);
		buf->len = len;
	}

	/* ack packets also get a special priority */
	if (flags & NET_ACK)
		buf->pri = 9;

	/* add it to out list */
	DQAdd(&conn->outlist, (DQNode*)buf);

	/* if it's urgent, do one retry now */
	if (GET_PRI(flags) > 5)
	{
		if (((int)conn->bytessince + buf->len + IP_UDP_OVERHEAD) <= limit)
		{
			buf->lastretry = current_millis();
			buf->retries++;
			SendRaw(conn, buf->d.raw, buf->len);
		}
#ifdef USE_HITLIMIT
		else
			conn->hitlimit = 1;
#endif
	}

	return buf;
}


void SendToOne(Player *p, byte *data, int len, int flags)
{
	ConnData *conn = PPDATA(p, connkey);
	if (!IS_OURS(p)) return;
	/* see if we can do it the quick way */
	if (len <= MAXPACKET)
	{
		pthread_mutex_lock(&conn->olmtx);
		BufferPacket(conn, data, len, flags, NULL, NULL);
		pthread_mutex_unlock(&conn->olmtx);
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
		    p != except &&
		    IS_OURS(p))
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
			ConnData *conn = PPDATA(p, connkey);
			if (!IS_OURS(p)) continue;
			pthread_mutex_lock(&conn->olmtx);
			BufferPacket(conn, data, len, flags, NULL, NULL);
			pthread_mutex_unlock(&conn->olmtx);
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
	ConnData *conn = PPDATA(p, connkey);
	/* we can't handle big packets here */
	assert(len < MAXPACKET);
	if (!IS_OURS(p)) return;

	pthread_mutex_lock(&conn->olmtx);
	BufferPacket(conn, data, len, NET_RELIABLE, callback, clos);
	pthread_mutex_unlock(&conn->olmtx);
}


void SendSized(Player *p, void *clos, int len,
		void (*req)(void *clos, int offset, byte *buf, int needed))
{
	ConnData *conn = PPDATA(p, connkey);
	struct sized_send_data *sd = amalloc(sizeof(*sd));
	if (!IS_OURS(p)) return;

	sd->request_data = req;
	sd->clos = clos;
	sd->totallen = len;
	sd->offset = 0;

	pthread_mutex_lock(&conn->olmtx);
	LLAdd(&conn->sizedsends, sd);
	pthread_mutex_unlock(&conn->olmtx);
}


i32 GetIP(Player *p)
{
	ConnData *conn = PPDATA(p, connkey);
	if (IS_OURS(p))
		return conn->sin.sin_addr.s_addr;
	else
		return -1;
}


void GetStats(struct net_stats *stats)
{
	if (stats)
		*stats = global_stats;
}

void GetClientStats(Player *p, struct net_client_stats *stats)
{
	ConnData *conn = PPDATA(p, connkey);

	if (!stats || !p) return;

#define ASSIGN(field) stats->field = conn->field
	ASSIGN(s2cn); ASSIGN(c2sn);
	ASSIGN(pktsent); ASSIGN(pktrecvd); ASSIGN(bytesent); ASSIGN(byterecvd);
	ASSIGN(pktdropped);
#undef ASSIGN
	/* encryption */
	if (conn->enc)
		stats->encname = conn->enc->head.name;
	else
		stats->encname = "none";
	/* convert to bytes per second */
	stats->limit = conn->limit;
	/* RACE: inet_ntoa is not thread-safe */
	astrncpy(stats->ipaddr, inet_ntoa(conn->sin.sin_addr), sizeof(stats->ipaddr));
	stats->port = conn->sin.sin_port;
}

int GetLastPacketTime(Player *p)
{
	ConnData *conn = PPDATA(p, connkey);
	return TICK_DIFF(current_ticks(), conn->lastpkt);
}


/* client connection stuff */

ClientConnection *MakeClientConnection(const char *addr, int port,
		Iclientconn *icc, Iclientencrypt *ice)
{
	struct
	{
		u8 t1, t2;
		u32 key;
		u16 type;
	} pkt;
	ClientConnection *cc = amalloc(sizeof(*cc));

	if (!icc || !addr) goto fail;

	cc->i = icc;
	cc->enc = ice;
	if (ice)
		cc->ced = ice->Init();

	InitConnData(&cc->c, NULL);

	cc->c.cc = cc; /* confusingly cryptic c code :) */

	cc->c.sin.sin_family = AF_INET;
	if (inet_aton((char*)addr, &cc->c.sin.sin_addr) == 0)
		goto fail;
	cc->c.sin.sin_port = htons((unsigned short)port);
	cc->c.whichsock = clientsock;

	pthread_mutex_lock(&ccmtx);
	LLAdd(&clientconns, cc);
	pthread_mutex_unlock(&ccmtx);

	pkt.t1 = 0x00;
#if 0
	pkt.t2 = 0x07;
	SendRaw(&cc->c, (byte*)&pkt, 2);
#endif
	pkt.key = random() | 0x80000000UL;
	pkt.t2 = 0x01;
	pkt.type = 0x0001;
	SendRaw(&cc->c, (byte*)&pkt, sizeof(pkt));

	return cc;

fail:
	mm->ReleaseInterface(ice);
	afree(cc);
	return NULL;
}

void SendPacket(ClientConnection *cc, byte *pkt, int len, int flags)
{
	if (len > MAXPACKET)
	{
		/* use 00 08/9 packets */
		byte buf[482], *dp = pkt;

		buf[0] = 0x00; buf[1] = 0x08;
		while (len > 480)
		{
			memcpy(buf+2, dp, 480);
			BufferPacket(&cc->c, buf, 482, flags | NET_RELIABLE, NULL, NULL);
			dp += 480;
			len -= 480;
		}
		buf[1] = 0x09;
		memcpy(buf+2, dp, len);
		BufferPacket(&cc->c, buf, len + 2, flags | NET_RELIABLE, NULL, NULL);
	}
	else
		BufferPacket(&cc->c, pkt, len, flags, NULL, NULL);
}

void DropClientConnection(ClientConnection *cc)
{
	byte pkt[] = { 0x00, 0x07 };
	SendRaw(&cc->c, pkt, sizeof(pkt));

	if (cc->enc)
	{
		cc->enc->Void(cc->ced);
		mm->ReleaseInterface(cc->enc);
		cc->enc = NULL;
	}

	pthread_mutex_lock(&ccmtx);
	LLRemove(&clientconns, cc);
	pthread_mutex_unlock(&ccmtx);
	afree(cc);
	lm->Log(L_INFO, "<net> (client connection) dropping client connection");
}

