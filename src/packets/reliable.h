
#ifndef __PACKETS_RELIABLE_H
#define __PACKETS_RELIABLE_H

/* reliable.h - reliable udp packets */

struct ReliablePacket
{
	i8 t1;
	i8 t2;
	i32 seqnum;
	byte data[0];
};

#endif

