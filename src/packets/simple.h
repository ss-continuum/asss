
#ifndef __PACKETS_SIMPLE_H
#define __PACKETS_SIMPLE_H

/* simple.h - generic packets */

struct SimplePacket
{
	i8 type;
	i16 d1, d2, d3, d4, d5;
};

struct SimplePacketA
{
	i8 type;
	i16 d[0];
};


#endif

