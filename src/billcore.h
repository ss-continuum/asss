
#ifndef __ASSS_H
#define __ASSS_H

#include "net.h"

/*
 * Ibillcore
 *
 * provides interface for sending and recieving packets from billing
 * server
 *
 */

#define BNET_NOBILLING S_FREE
#define BNET_CONNECTED S_CONNECTED

typedef struct Ibillcore
{
	void (*SendToBiller)(byte *data, int length, int flags);
	void (*AddPacket)(byte pktype, PacketFunc func);
	void (*RemovePacket)(byte pktype, PacketFunc func);
	int (*GetStatus)();
} Ibillcore;

#endif

