
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "asss.h"


/* MACROS */

#define MAXTYPES 128
#define MAXENCRYPT 4

#define NET_INPRESIZE 2
#define NET_INBIGPKT 4

/* STRUCTS */

#include "packets/reliable.h"

#include "packets/timesync.h"

typedef struct ClientData
{
	int s2cn, c2sn, flags;
	struct sockaddr_in sin;
	unsigned int lastpkt, key;
	short enctype;
	int bigpktsize, bigpktroom;
	byte *bigpktbuf;
} ClientData;

typedef struct OutBufData
{
	/* (retries == 0) means empty entry */
	int retries, pid, len;
	unsigned int lastretry;
	u8 t[2];
	i32 seqnum;
	byte data[MAXPACKET];
} OutBufData;


typedef struct InBufData
{
	int filled, pid, len;
	u8 t[2];
	i32 seqnum;
	byte data[MAXPACKET];
} InBufData;


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
local inline void SendRaw(int, byte *, int);
local void BufferPacket(int, byte *, int, int);
local void RecvPacket();
local void CheckBuffers();
local void ProcessKey(int, byte *, int);
local void ProcessKeyResponse(int, byte *, int);
local void ProcessReliable(int, byte *, int);
local void ProcessGrouped(int, byte *, int);
local void ProcessResponse(int, byte *, int);
local void ProcessSyncRequest(int, byte *, int);
local void ProcessBigData(int, byte *, int);
local void ProcessPresize(int, byte *, int);
local void ProcessDrop(int, byte *, int);
local void RecvOtherPackets();
local void InitSockets();



/* GLOBAL DATA */

local Ilogman *log;
local Iconfig *cfg;
local PlayerData *players;

local int mysock, myothersock, mybillingsock;
local OutBufData *outbuf;
local InBufData *inbuf;
/* global clients struct!: */
local ClientData clients[MAXPLAYERS+EXTRA_PID_COUNT];

local LinkedList *handlers[MAXTYPES];
local Iencrypt *encrypt[MAXENCRYPT];

local int cfg_port, cfg_retries, cfg_timeout, cfg_outbuflen, cfg_inbuflen;
local int cfg_selectusec, cfg_process, cfg_biglimit, cfg_encmode, cfg_usebiller;
local int cfg_droptimeout, cfg_billping;

local int pcountpings;

local Inet _int =
{
	SendToOne, SendToArena, SendToSet, SendToAll,
	DropClient, ProcessPacket, AddPacket, RemovePacket,
	NewConnection, GetIP
};


local PacketFunc oohandlers[] =
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




/* START OF FUNCTIONS */


int MM_net(int action, Imodman *mm)
{
	int i;

	if (action == MM_LOAD)
	{
		players = mm->players;
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_LOGMAN, &log);
		if (!cfg || !log) return MM_FAIL;

		for (i = 0; i < MAXTYPES; i++)
			handlers[i] = LLAlloc();
		for (i = 0; i < MAXENCRYPT; i++)
			mm->RegInterest(I_ENCRYPTBASE + i, encrypt + i);

		/* store configuration params */
		cfg_port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
		cfg_retries = cfg->GetInt(GLOBAL, "Net", "ReliableRetries", 5);
		cfg_timeout = cfg->GetInt(GLOBAL, "Net", "ReliableTimeout", 150);
		cfg_outbuflen = cfg->GetInt(GLOBAL, "Net", "OutBufferSize", 100);
		cfg_inbuflen = cfg->GetInt(GLOBAL, "Net", "InBufferSize", 100);
		cfg_selectusec = cfg->GetInt(GLOBAL, "Net", "SelectUSec", 10000);
		cfg_process = cfg->GetInt(GLOBAL, "Net", "ProcessGroup", 5);
		cfg_biglimit = cfg->GetInt(GLOBAL, "Net", "BigLimit", 1);
		cfg_encmode = cfg->GetInt(GLOBAL, "Net", "EncryptMode", 0);
		cfg_droptimeout = cfg->GetInt(GLOBAL, "Net", "DropTimeout", 3000);
		cfg_usebiller = cfg->GetInt(GLOBAL, "Billing", "UseBilling", 0);
		cfg_billping = cfg->GetInt(GLOBAL, "Billing", "PingTime", 3000);

		/* allocate buffers */
		outbuf = amalloc(cfg_outbuflen * sizeof(OutBufData));
		inbuf = amalloc(cfg_inbuflen * sizeof(InBufData));
		
		/* get the sockets */
		InitSockets();
		pcountpings = 0;

		/* install our main loop entry points */
		for (i = 0; i < cfg_process; i++)
			mm->RegCallback(CALLBACK_MAINLOOP, RecvPacket);
		mm->RegCallback(CALLBACK_MAINLOOP, CheckBuffers);
		mm->RegCallback(CALLBACK_MAINLOOP, RecvOtherPackets);

		/* install ourself */
		mm->RegInterface(I_NET, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		mm->UnregInterface(I_NET, &_int);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_LOGMAN, &log);
		for (i = 0; i < MAXENCRYPT; i++)
			mm->UnregInterest(I_ENCRYPTBASE + i, encrypt + i);

		/* get rid of main loop entries */
		for (i = 0; i < cfg_process; i++)
			mm->UnregCallback(CALLBACK_MAINLOOP, RecvPacket);
		mm->UnregCallback(CALLBACK_MAINLOOP, CheckBuffers);
		mm->UnregCallback(CALLBACK_MAINLOOP, RecvOtherPackets);

		/* free memory */
		for (i = 0; i < MAXTYPES; i++) LLFree(handlers[i]);
		afree(inbuf); afree(outbuf);

		close(mysock);
		close(myothersock);
	}
	return MM_OK;
}


void AddPacket(byte t, PacketFunc f)
{
	if (t >= MAXTYPES) return;
	LLAdd(handlers[t], f);
}

void RemovePacket(byte t, PacketFunc f)
{
	if (t >= MAXTYPES) return;
	LLRemove(handlers[t], f);
}


i32 GetIP(int pid)
{
	return clients[pid].sin.sin_addr.s_addr;
}


void InitSockets()
{
	struct sockaddr_in localsin;

	localsin.sin_family = AF_INET;
	memset(localsin.sin_zero,0,sizeof(localsin.sin_zero));
	localsin.sin_addr.s_addr = INADDR_ANY;
	localsin.sin_port = htons(cfg_port);

	if ((mysock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_NORMAL,"net: socket");
	if (bind(mysock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_NORMAL,"net: bind");

	localsin.sin_port = htons(cfg_port+1);
	if ((myothersock = socket(PF_INET,SOCK_DGRAM,0)) == -1)
		Error(ERROR_NORMAL,"net: socket");
	if (bind(myothersock, (struct sockaddr *) &localsin, sizeof(localsin)) == -1)
		Error(ERROR_NORMAL,"net: bind");

	memset(clients + PID_BILLER, 0, sizeof(ClientData));
	if (cfg_usebiller)
	{
		/* get socket */
		if ((mybillingsock = socket(PF_INET, SOCK_DGRAM, 0)) == -1)
			Error(ERROR_NORMAL, "could not allocate billing socket");

		/* set up billing client struct */
		memset(players + PID_BILLER, 0, sizeof(PlayerData));
		memset(clients + PID_BILLER, 0, sizeof(ClientData));
		players[PID_BILLER].status = BNET_NOBILLING;
		strcpy(players[PID_BILLER].name, "<<Billing Server>>");
		clients[PID_BILLER].c2sn = -1;
		clients[PID_BILLER].sin.sin_family = AF_INET;
		clients[PID_BILLER].sin.sin_addr.s_addr =
			inet_addr(cfg->GetStr(GLOBAL, "Billing", "IP"));
		clients[PID_BILLER].sin.sin_port = 
			htons(cfg->GetInt(GLOBAL, "Billing", "Port", 1850));
		memset(clients[PID_BILLER].sin.sin_zero, 0, sizeof(localsin.sin_zero));
	}
}


void RecvOtherPackets()
{
	struct sockaddr_in sin;
	struct timeval tv = { 0, 0 };
	fd_set fds;
	int n, maxfd, data[2], sinsize = sizeof(sin);
	static byte buffer[MAXPACKET];

	FD_ZERO(&fds);
	FD_SET(myothersock, &fds); maxfd = myothersock;

	if (cfg_usebiller)
	{	/* add billing socket to fds */
		FD_SET(mybillingsock, &fds);
		if (mybillingsock > maxfd) maxfd = mybillingsock;
	}

	n = select(maxfd+1, &fds, NULL, NULL, &tv);

	if (FD_ISSET(myothersock, &fds))
	{	/* data on port + 1 */
		n = recvfrom(myothersock, (char*)data, 4, 0, &sin, &sinsize);
		data[1] = data[0];
		if (n != 4) return;
		for (data[0] = 0, n = 0; n < MAXPLAYERS; n++)
			if (players[n].status == S_CONNECTED)
				data[0]++;
		sendto(myothersock, (char*)data, 8, 0, &sin, sinsize);
		pcountpings++;
	}

	if (cfg_usebiller && FD_ISSET(mybillingsock, &fds))
	{	/* data from billing server */
		n = recvfrom(mybillingsock, buffer, MAXPACKET, 0, &sin, &sinsize);
		/*log->Log(LOG_DEBUG, "%i bytes from billing server", n); */
		if (memcmp(&sin, &clients[PID_BILLER].sin, sinsize))
			log->Log(LOG_BADDATA,
					"Data recieved on billing server socket from incorrect origin: %s:%i",
					inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
		else if (n > 0)
		{
			clients[PID_BILLER].lastpkt = GTC();
			ProcessPacket(PID_BILLER, buffer, n);
		}
	}
}


void RecvPacket()
{
	static byte recvbuf[MAXPACKET];
	struct sockaddr_in sin;
	struct timeval tv = { 0, cfg_selectusec };
	fd_set fds;
	int l, i, sinsize = sizeof(sin), type;

	FD_ZERO(&fds);
	FD_SET(mysock, &fds);

	l = select(mysock+1, &fds, NULL, NULL, &tv);
	if (l == 0) return;

	l = recvfrom(mysock, recvbuf, MAXPACKET, 0, &sin, &sinsize);

	if (l > 0)
	{
		/*printf("packet from %i:%i, p[0].status = %i\n", sin.sin_addr.s_addr, sin.sin_port,
				players[0].status); */
		
		i = 0;
		/* search for an existing connection */
		while (	(players[i].status == S_FREE ||
				( clients[i].sin.sin_addr.s_addr != sin.sin_addr.s_addr ||
				  clients[i].sin.sin_port != sin.sin_port) ) &&
				i < MAXPLAYERS) i++;  /* what a big ugly mess :)  should use hash table */

		if (i == MAXPLAYERS)
		{	/* new client */
			i = NewConnection(&sin);
			if (i == -1)
			{
				char pk7[] = { 0x00, 0x07 };
				sendto(mysock, &pk7, 2, 0, &sin, sinsize);
				log->Log(LOG_IMPORTANT,"Too many players! Dropping extra connections!");
				return;
			}
			else
				log->Log(LOG_DEBUG,"New connection (%s:%i) assigning pid %i",
						inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), i);
		}

		clients[i].lastpkt = GTC();

		/* decrypt the packet */
		type = clients[i].enctype;
		if (type >= 0 && encrypt[type])
		{
			/*log->Log(LOG_DEBUG,"calling decrypt: %X %X", recvbuf[0], recvbuf[1]); */
			if (recvbuf[0] == 0x00)
				encrypt[type]->Decrypt(i, recvbuf+2, l-2);
			else
				encrypt[type]->Decrypt(i, recvbuf+1, l-1);
		}

		if (recvbuf[0] == 0x00 && recvbuf[1] < sizeof(oohandlers) && oohandlers[recvbuf[1]])
			(oohandlers[recvbuf[1]])(i, recvbuf, l);
		else if (recvbuf[0] < PKT_BILLBASE)
		{
			LinkedList *lst = handlers[recvbuf[0]];
			Link *lnk;
			for (lnk = LLGetHead(lst); lnk; lnk = lnk->next)
				((PacketFunc)lnk->data)(i, recvbuf, l);
		}
	}
}


void ProcessPacket(int pid, byte *buf, int len)
{
	if (buf[0] == 0x00 && buf[0] < sizeof(oohandlers) && oohandlers[buf[1]])
		(oohandlers[buf[1]])(pid, buf, len);
	else if (buf[0] < PKT_BILLBASE)
	{
		LinkedList *lst = handlers[buf[0]];
		Link *l;

		if (pid == PID_BILLER)
			lst = handlers[buf[0] + PKT_BILLBASE];

		for (l = LLGetHead(lst); l; l = l->next)
			((PacketFunc)l->data)(pid, buf, len);
	}
}


int NewConnection(struct sockaddr_in *sin)
{
	int i = 0;

	while (players[i].status != S_FREE && i < MAXPLAYERS) i++;
	if (i == MAXPLAYERS) return -1;
	memset(clients + i, 0, sizeof(ClientData));
	clients[i].c2sn = -1;
	clients[i].enctype = -1;
	if (sin)
		memcpy(&clients[i].sin, sin, sizeof(struct sockaddr_in));
	else
		clients[i].flags = NET_FAKE;
	memset(players + i, 0, sizeof(PlayerData));
	players[i].type = S2C_PLAYERENTERING; /* restore type */
	players[i].status = S_CONNECTED;
	players[i].arena = -1;
	return i;
}


void ProcessKey(int pid, byte *buf, int len)
{
	int key = *(int*)(buf+2);
	short type = *(short*)(buf+6);

	buf[1] = 2;

	if (cfg_encmode == 0)
	{
		SendRaw(pid, buf, 6);
	}
	else if (type >= 0 && encrypt[type])
	{
		key = encrypt[type]->Respond(key);
		*(int*)(buf+2) = key;
		SendRaw(pid, buf, 6);
		encrypt[type]->Init(pid, key);
		clients[pid].enctype = type;
	}
	else
		log->Log(LOG_BADDATA, "Unknown encryption type attempted to connect");
}


void ProcessKeyResponse(int pid, byte *buf, int len)
{
	if (pid != PID_BILLER)
		log->Log(LOG_BADDATA, "Key response from non-billing server!");
	else
	{
		Link *l;

		players[pid].status = BNET_CONNECTED;
		clients[pid].lastpkt = GTC();

		for (l = LLGetHead(handlers[PKT_BILLBASE + 0]);
				l; l = l->next)
			((PacketFunc)l->data)(pid, buf, len);
	}
}


void ProcessReliable(int pid, byte *d, int len)
{
	int i = 0, snum = ((struct ReliablePacket*)d)->seqnum;

	/* ack, then drop duplicated packets */
	if (snum <= clients[pid].c2sn)
	{
		d[1] = 0x04;
		BufferPacket(pid, d, 6, NET_UNRELIABLE);
		return;
	}

	/* find open spot in the buffer */
	while (inbuf[i].filled && i < cfg_inbuflen) i++;

	if (i < cfg_inbuflen) /* if there are any buffers left */
	{
		int c, t, moved, order[cfg_inbuflen];

		d[1] = 0x04; /* ack it immediately */
		BufferPacket(pid, d, 6, NET_UNRELIABLE);

		memcpy(inbuf[i].t,d,len); /* put the data in the buffer */
		inbuf[i].filled = 1;
		inbuf[i].pid = pid;
		inbuf[i].len = len-6;

		/* look for a sequence of packets: */
		/* c is the length of the found sequence */
		/* t is the target sequence number */
		/* moved is a flag to see if we're done */
		c = 0; t = clients[pid].c2sn+1; moved = 1;
		while (moved)
		{
			for (moved = 0, i = 0; i < cfg_inbuflen; i++)
				if (	inbuf[i].filled	&&
						inbuf[i].pid == pid &&
						inbuf[i].seqnum == t)
				{
					moved = 1; t++; order[c++] = i;
				}
		}

		for (i = 0; i < c; i++)	/* if we have found a sequence (even a single */
		{						/* packet) then process them (in order) */
			int x = order[i];
			ProcessPacket(pid, inbuf[x].data, inbuf[x].len);
			inbuf[x].filled = 0;
			clients[pid].c2sn = inbuf[x].seqnum;
		}
	}
	else
		log->Log(LOG_ERROR,"Input buffer full! Dropping packets!");
}


void ProcessGrouped(int pid, byte *p, int n)
{
	int pos = 2, len = 1;
	while (pos < n && len > 0)
	{
		len = p[pos++];
		ProcessPacket(pid,p+pos,len);
		pos += len;
	}
}


void ProcessResponse(int pid, byte *p, int n)
{
	int snum = ((struct ReliablePacket*)p)->seqnum, i;
	
	for (i = 0; i < cfg_outbuflen; i++)
		if (outbuf[i].pid == pid && outbuf[i].seqnum == snum)
			outbuf[i].retries = 0;
}


void ProcessSyncRequest(int pid, byte *p, int n)
{
	struct TimeSyncC2S *cts = (struct TimeSyncC2S*)p;
	struct TimeSyncS2C ts = { 0x00, 0x06, cts->time, GTC() };
	BufferPacket(pid, (byte*)&ts, sizeof(ts), NET_UNRELIABLE);
}


void ProcessDrop(int pid, byte *p, int n)
{
	int i;
	byte leaving = C2S_LEAVING;

	if (pid == PID_BILLER)
	{
		log->Log(LOG_IMPORTANT, "Connection to billing server lost");
		players[pid].status = BNET_NOBILLING;
	}
	else
	{
		/* leave arena again, just in case */
		if (players[pid].arena >= 0)
			ProcessPacket(pid, &leaving, 1);

		players[pid].status = S_FREE; /* set free status */

		for (i = 0; i < cfg_outbuflen; i++) /* kill pending data */
			if (outbuf[i].pid == pid)
				outbuf[i].retries = 0;

		log->Log(LOG_INFO,"Player disconnected (%s)", players[pid].name);
	}
}


void ProcessBigData(int pid, byte *p, int n)
{
	int newsize = clients[pid].bigpktsize + n - 2;
	byte *newbuf;

	if (clients[pid].flags & NET_INPRESIZE)
	{
		log->Log(LOG_BADDATA, "Recieved bigpacket while handling presized data! (%s)",
				players[pid].name);
	}

	clients[pid].flags |= NET_INBIGPKT;
	
	if (newsize > MAXBIGPACKET)
	{
		log->Log(LOG_BADDATA,
			"Big packet: refusing to allocate more than %i bytes (%s)",
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

	memcpy(newbuf + clients[pid].bigpktsize, p+2, n-2);

	clients[pid].bigpktbuf = newbuf;
	clients[pid].bigpktsize = newsize;

	if (p[1] == 0x08) return;

	ProcessPacket(pid, newbuf, newsize);

freebigbuf:
	afree(clients[pid].bigpktbuf);
	clients[pid].bigpktbuf = NULL;
	clients[pid].bigpktsize = 0;
	clients[pid].bigpktroom = 0;
	clients[pid].flags &= ~NET_INBIGPKT;
}


void ProcessPresize(int pid, byte *p, int len)
{
	struct ReliablePacket *pk = (struct ReliablePacket *)p;
	int size = pk->seqnum;

	if (clients[pid].flags & NET_INBIGPKT)
	{
		log->Log(LOG_BADDATA,"Recieved presized data while handling bigpacket! (%s)",
				players[pid].name);
		return;
	}

	if (clients[pid].bigpktbuf)
	{	/* copy data */
		if (size != clients[pid].bigpktroom)
		{
			log->Log(LOG_BADDATA, "Presized data length mismatch! (%s)",
					players[pid].name);
			goto freepacket;
		}
		memcpy(clients[pid].bigpktbuf+clients[pid].bigpktsize, pk->data, len-6);
		clients[pid].bigpktsize += (len-6);
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
			memcpy(clients[pid].bigpktbuf, pk->data, len-6);
			clients[pid].bigpktsize = len-6;
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
}


#define MAXGROUPED 20
void CheckBuffers()
{
	static unsigned int pcount[MAXPLAYERS+1];
	static unsigned int order[MAXPLAYERS+1][MAXGROUPED], bigcount[MAXPLAYERS+1];
	static byte buf[MAXPACKET];
	unsigned int gtc = GTC(), i, j;
	byte *current;

	/* zero some arrays */
	memset(pcount, 0, (MAXPLAYERS+1) * sizeof(unsigned int));
	memset(bigcount, 0, (MAXPLAYERS+1) * sizeof(unsigned int));

	/* find packets to group */
	for (i = 0; i < cfg_outbuflen; i++)
		if (outbuf[i].retries && (gtc - outbuf[i].lastretry) > cfg_timeout)
		{
			if (outbuf[i].len > 240)
			{	/* too big for 000E */
				if (bigcount[outbuf[i].pid]++ < cfg_biglimit)
				{
					outbuf[i].lastretry = gtc;
					outbuf[i].retries--;
					SendRaw(outbuf[i].pid, outbuf[i].t, outbuf[i].len);
				}
			}
			else if (pcount[outbuf[i].pid] < MAXGROUPED)
					order[outbuf[i].pid][pcount[outbuf[i].pid]++] = i;
				/* packets over the group limit don't get sent */
				/* at all until the first bunch are ack'd */
		}

	/* send them */
	for (i = 0; i < (MAXPLAYERS + EXTRA_PID_COUNT); i++)
	{
		if (pcount[i] == 1)
		{	/* only one packet for this person, send it */
			j = order[i][0];
			outbuf[j].lastretry = gtc;
			outbuf[j].retries--;
			SendRaw(outbuf[j].pid, outbuf[j].t, outbuf[j].len);
		}
		else if (pcount[i] > 1)
		{	/* group all the packets to send */
			int k;
			current = buf+2;
			buf[0] = 0x00; buf[1] = 0x0E;
			for (j = 0; j < pcount[i]; j++)
			{
				k = order[i][j];
				if ((current-buf+outbuf[k].len+5) < MAXPACKET)
				{
					*current++ = outbuf[k].len;
					memcpy(current, outbuf[k].t, outbuf[k].len);
					current += outbuf[k].len;
					outbuf[k].lastretry = gtc;
					outbuf[k].retries--;
				}
				else j = MAXGROUPED;
			}
			SendRaw(i, buf, current - buf);
		}

		if (players[i].status == S_CONNECTED && (gtc - clients[i].lastpkt) > cfg_droptimeout)
		{
			byte two = C2S_LEAVING;
			if (i < MAXPLAYERS)
				ProcessPacket(i, &two, 1); /* alert the rest of the arena */
			log->Log(LOG_INFO,"Player timed out (%s)", players[i].name);
			DropClient(i);
		}
	}
	/*
	if (	players[PID_BILLER].status == BNET_CONNECTED &&
			(gtc - clients[PID_BILLER].lastpkt) > (cfg_billping * 2))
	{
		log->Log(LOG_IMPORTANT, "Billing server connection timed out, dropping");
		DropClient(PID_BILLER);
	}
	*/
}


void DropClient(int pid)
{
	byte pkt1[2] = {0x00, 0x07};

	/* hack: should use a different constant for PID_BILLER */
	if (players[pid].status != S_FREE)
	{
		SendRaw(pid, pkt1, 2);
		/* pretend the client initiated the disconnection: */
		ProcessPacket(pid, pkt1, 2);
	}
}


void SendRaw(int pid, byte *data, int len)
{
	static byte encbuf[MAXPACKET];
	int type = clients[pid].enctype;

	if (clients[pid].flags & NET_FAKE) return;

	if (pid == PID_BILLER)
	{
		sendto(mybillingsock, data, len, 0, &clients[pid].sin, sizeof(struct sockaddr_in));
	}
	else
	{
		memcpy(encbuf, data, len);

		if (type >= 0 && encrypt[type])
		{
			if (data[0] == 0x00)
				encrypt[type]->Encrypt(pid, encbuf+2, len-2);
			else
				encrypt[type]->Encrypt(pid, encbuf+1, len-1);
		}

		sendto(mysock, encbuf, len, 0, &clients[pid].sin, sizeof(struct sockaddr_in));
	}
}


void BufferPacket(int pid, byte *data, int len, int rel)
{
	int i = 0;

	if (clients[pid].flags & NET_FAKE) return;

	while (outbuf[i].retries && i < cfg_outbuflen) i++;
	if (i < cfg_outbuflen)
	{
		memcpy(outbuf[i].t, data, len);
		outbuf[i].retries = cfg_retries;
		outbuf[i].pid = pid;
		outbuf[i].len = len;
		if (rel & NET_IMMEDIATE)
		{
			SendRaw(pid, data, len);
			outbuf[i].lastretry = GTC();
			outbuf[i].retries--;
		}
		else
			outbuf[i].lastretry = 0;

		if (!(rel & NET_RELIABLE))
			outbuf[i].retries = 1;
	}
	else
	{
		log->Log(LOG_ERROR,"Outgoing buffer full! Some packets will not be buffered!");
		SendRaw(pid, data, len); /* sending without buffering is better than not sending */
	}
}


void SendToOne(int pid, byte *data, int len, int reliable)
{
	int set[2] = {pid, -1};
	SendToSet(set, data, len, reliable);
}


void SendToArena(int arena, int except, byte *data, int len, int reliable)
{
	int set[MAXPLAYERS+1], i, p = 0;
	if (arena < 0) return;
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_CONNECTED && players[i].arena == arena && i != except)
			set[p++] = i;
	set[p] = -1;
	SendToSet(set, data, len, reliable);
}


void SendToAll(byte *data, int len, int reliable)
{
	int set[MAXPLAYERS+1], i, p = 0;
	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_CONNECTED)
			set[p++] = i;
	set[p] = -1;
	SendToSet(set, data, len, reliable);
}


void SendToSet(int *set, byte *data, int len, int rel)
{
	int i = 0;

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
	else if (rel & NET_RELIABLE)
	{
		struct ReliablePacket *pk = alloca(len+6);

		memcpy(pk->data, data, len);
		pk->t1 = 0x00; pk->t2 = 0x03;

		while (set[i] != -1)
		{
			pk->seqnum = clients[set[i]].s2cn++;
			BufferPacket(set[i], (byte*)pk, len+6, rel);
			i++;
		}
	}
	else
	{
		while (set[i] != -1)
			BufferPacket(set[i++], data, len, rel);
	}
}


