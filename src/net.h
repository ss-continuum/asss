
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
 * NewConnection lets you make a fake entry in the network module's
 * client table. this can be used for running bots internally, or for
 * simulating clients.
 *
 */


/* included for struct sockaddr_in */
#include <netinet/in.h>


#define S_FREE 0
#define S_CONNECTED 1

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


typedef struct Inet
{
	void (*SendToOne)(int pid, byte *data, int length, int flags);
	void (*SendToArena)(int arenaid, int exception, byte *data, int length, int flags);
	void (*SendToSet)(int *pidset, byte *data, int length, int flags);
	void (*SendToAll)(byte *data, int length, int flags);
	void (*DropClient)(int pid);
	void (*ProcessPacket)(int pid, byte *data, int length);
	void (*AddPacket)(byte pktype, PacketFunc func);
	void (*RemovePacket)(byte pktype, PacketFunc func);
	int (*NewConnection)(struct sockaddr_in *sin);
	i32 (*GetIP)(int pid);
} Inet;


#endif

