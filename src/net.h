
/* dist: public */

#ifndef __NET_H
#define __NET_H

/*
 * Inet - network stuff
 *
 * need more docs here
 *
 * the player will be locked during the duration of the packet function.
 * so you don't have to do it yourself. you do have to do it if you're
 * modifying the player struct not in a packet handler, or if you're
 * modifying other players in another player's handler.
 *
 */


/* included for struct sockaddr_in */
#ifndef WIN32
#include <netinet/in.h>
#else
#endif


/* important constants */
#define MAXPACKET 512
#define MAXBIGPACKET CFG_MAX_BIG_PACKET

/* bits in the flags parameter to the SendX functions */
#define NET_UNRELIABLE 0x00
#define NET_RELIABLE 0x01
#define NET_DROPPABLE 0x02

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



typedef void (*PacketFunc)(Player *p, byte *data, int length);
typedef void (*SizedPacketFunc)
	(Player *p, byte *data, int len, int offset, int totallen);

typedef void (*RelCallback)(Player *p, int success, void *clos);

#define CB_CONNINIT "conninit"
typedef void (*ConnectionInitFunc)(struct sockaddr_in *sin, byte *pkt, int len, void *v);


struct net_stats
{
	unsigned int pcountpings, pktsent, pktrecvd;
	unsigned int buffercount, buffersused;
	unsigned int pri_stats[8];
};

struct net_client_stats
{
	/* sequence numbers */
	i32 s2cn, c2sn;
	/* counts of stuff sent and recvd */
	unsigned long pktsent, pktrecvd, bytesent, byterecvd;
	/* count of s2c packets dropped */
	unsigned long pktdropped;
	/* encryption type and bandwidth limit */
	unsigned int limit;
	const char *encname;
	/* ip info */
	char ipaddr[16];
	unsigned short port;
};


#include "encrypt.h"


#define I_NET "net-7"

typedef struct Inet
{
	INTERFACE_HEAD_DECL

	void (*SendToOne)(Player *p, byte *data, int length, int flags);
	void (*SendToArena)(Arena *a, Player *except, byte *data, int length, int flags);
	void (*SendToSet)(LinkedList *set, byte *data, int length, int flags);
	void (*SendToTarget)(const Target *target, byte *data, int length, int flags);
	void (*SendWithCallback)(Player *p, byte *data, int length,
			RelCallback callback, void *clos);
	void (*SendSized)(Player *p, void *clos, int len,
			void (*request_data)(void *clos, int offset, byte *buf, int needed));

	void (*DropClient)(Player *p);

	void (*AddPacket)(int pktype, PacketFunc func);
	void (*RemovePacket)(int pktype, PacketFunc func);
	void (*AddSizedPacket)(int pktype, SizedPacketFunc func);
	void (*RemoveSizedPacket)(int pktype, SizedPacketFunc func);

	/* only to be used by encryption modules! */
	void (*ReallyRawSend)(struct sockaddr_in *sin, byte *pkt, int len, void *v);
	Player * (*NewConnection)(int type, struct sockaddr_in *sin, Iencrypt *enc, void *v);

	void (*GetStats)(struct net_stats *stats);
	void (*GetClientStats)(Player *p, struct net_client_stats *stats);
	int (*GetLastPacketTime)(Player *p);

	int (*GetListenData)(unsigned index, int *port, char *connectasbuf, int buflen);
	/* returns true if it returned stuff, false if bad index */
} Inet;


#endif

