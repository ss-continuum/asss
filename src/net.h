
#ifndef __NET_H
#define __NET_H

/*
 * Inet - network stuff
 *
 * the SendTo* functions are used to send packets to clients. SendToOne
 * sends something to a specific person. SendToArena sends to a whole
 * arena at once (with the option of excluding up to one person).
 * SendToSet lets you specify a set of pids to send to (as an array,
 * terminated by -1). SendToAll (rarely used) does what it says.
 *
 * if you're sending packets to more than one person, use the multiple
 * send functions. they're there for a reason. (they will minimize
 * copying of memory)
 *
 * DropClient will kick someone. use with care.
 *
 * ProcessPacket allows you to inject a fake packet into the
 * packet-processing pipeline. it will be processed synchronously.
 *
 * Add/RemovePacket add and remove your packet-processing functions. the
 * function will be called with a pid, a pointer to the packet data, and
 * the length of the data.
 *
 * REALLY IMPORTANT: the player will be locked during the duration of
 * the packet function. so you don't have to do it yourself. you do have
 * to do it if you're modifying the player struct not in a packet
 * handler, or if you're modifying other players in another player's
 * handler.
 *
 * NewConnection lets you make a fake entry in the network module's
 * client table. this can be used for running bots internally, or for
 * simulating clients. it's rather crude at the moment: you have to
 * simulate just about everything including login packets, etc.
 *
 */


/* included for struct sockaddr_in */
#ifndef WIN32
#include <netinet/in.h>
#else
#include <winsock.h>
#endif


#include "encrypt.h"


#define EXTRA_PID_COUNT 1
#define EXTRA_PID(x) ((MAXPLAYERS)+x)

#define PID_BILLER EXTRA_PID(0)
#define PID_DIRECTORY EXTRA_PID(1)

#define PKT_BILLER_OFFSET 0x100

/* bits in the flags parameter to the SendX functions */
#define NET_UNRELIABLE 0x00
#define NET_RELIABLE 0x01

/* priority levels are encoded using bits 3-5 of the flags parameter
 * (0x04, 0x08, 0x10). priority is unrelated to reliable-ness.
 * priorities 4 and higher mean the packet is "urgent." urgent packets
 * are sent synchronously, if possible.
 * suggested use of priorities:
 * -1 - stuff that really doesn't matter (e.g. public macros)
 *  0 - most stuff (e.g. chat)
 * +1 - important stuff (e.g. ship/freq changes)
 * +2 - far position packets
 * +3 - far weapons packets
 * +4 - close position packets, bricks
 * +5 - close weapons packets
 */
#define NET_PRI_N1      0x04
#define NET_PRI_ZERO    0x08
#define NET_PRI_P1      0x0C
#define NET_PRI_P2      0x10
#define NET_PRI_P3      0x14
#define NET_PRI_P4      0x18
#define NET_PRI_P5      0x1C

#define NET_PRI_DEFAULT NET_PRI_ZERO
#define NET_PRI_URGENT NET_PRI_P4

/* turns a flags value into a priority value from 0 to 7 */
#define GET_PRI(flags) (((flags) & 0x1C) >> 2)


typedef void (*PacketFunc)(int pid, byte *data, int length);

typedef void (*SizedPacketFunc)
	(int pid, byte *data, int len, int offset, int totallen);

typedef void (*RelCallback)(int pid, int success, void *clos);

#define CB_CONNINIT ("conninit")
typedef void (*ConnectionInitFunc)(struct sockaddr_in *sin, byte *pkt, int len);


struct net_stats
{
	unsigned int pcountpings, pktsent, pktrecvd;
	unsigned int buffercount, buffersused;
	unsigned int pri_stats[8];
};

struct client_stats
{
	/* sequence numbers */
	int s2cn, c2sn;
	/* counts of stuff sent and recvd */
	unsigned int pktsent, pktrecvd, bytesent, byterecvd;
	/* encryption type, connect time, and bandwidth limit */
	unsigned int connecttime, limit;
	const char *encname;
	/* ip info */
	char ipaddr[16];
	unsigned short port;
};


#define I_NET "net-2"

typedef struct Inet
{
	INTERFACE_HEAD_DECL

	void (*SendToOne)(int pid, byte *data, int length, int flags);
	void (*SendToArena)(int arenaid, int exception, byte *data, int length, int flags);
	void (*SendToSet)(int *pidset, byte *data, int length, int flags);
	void (*SendToTarget)(const Target *target, byte *data, int length, int flags);
	void (*SendToAll)(byte *data, int length, int flags);
	void (*SendWithCallback)(int *pidset, byte *data, int length,
			RelCallback callback, void *clos);
	void (*SendSized)(int pid, void *clos, int len,
			void (*request_data)(void *clos, int offset, byte *buf, int needed));

	/* only to be used by encryption modules! */
	void (*ReallyRawSend)(struct sockaddr_in *sin, byte *pkt, int len);

	void (*DropClient)(int pid);

	void (*ProcessPacket)(int pid, byte *data, int length);

	void (*AddPacket)(int pktype, PacketFunc func);
	void (*RemovePacket)(int pktype, PacketFunc func);
	void (*AddSizedPacket)(int pktype, SizedPacketFunc func);
	void (*RemoveSizedPacket)(int pktype, SizedPacketFunc func);

	int (*NewConnection)(int type, struct sockaddr_in *sin, Iencrypt *enc);

	/* bandwidth limits. the units on these parameters are bytes/second */
	void (*SetLimit)(int pid, int limit);

	void (*GetStats)(struct net_stats *stats);
	void (*GetClientStats)(int pid, struct client_stats *stats);
} Inet;


#endif

