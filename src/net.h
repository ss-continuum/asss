
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
 * the flags argument should be either NET_UNRELIABLE or NET_RELIABLE.
 * the other flags, NET_IMMEDIATE and NET_PRESIZE can be |'ed with one
 * of those.
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


#define EXTRA_PID_COUNT 1
#define EXTRA_PID(x) ((MAXPLAYERS)+x)

#define PID_BILLER EXTRA_PID(0)
#define PID_DIRECTORY EXTRA_PID(1)

#define PKT_BILLBASE 0x50

#define NET_UNRELIABLE 0
#define NET_RELIABLE 1
#define NET_IMMEDIATE 2
#define NET_PRESIZE 4

#define NET_FAKE 1


typedef void (*PacketFunc)(int pid, byte *data, int length);

struct net_stats
{
	int pcountpings, pktssent, pktsrecvd;
	int buffercount, buffersused;
};


typedef struct Inet
{
	INTERFACE_HEAD_DECL
	void (*SendToOne)(int pid, byte *data, int length, int flags);
	void (*SendToArena)(int arenaid, int exception, byte *data, int length, int flags);
	void (*SendToSet)(int *pidset, byte *data, int length, int flags);
	void (*SendToAll)(byte *data, int length, int flags);
	void (*DropClient)(int pid);
	void (*ProcessPacket)(int pid, byte *data, int length);
	void (*AddPacket)(byte pktype, PacketFunc func);
	void (*RemovePacket)(byte pktype, PacketFunc func);
	int (*NewConnection)(int type, struct sockaddr_in *sin);
	i32 (*GetIP)(int pid);
	void (*GetStats)(struct net_stats *stats);
} Inet;


#endif

