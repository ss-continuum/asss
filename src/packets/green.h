
#ifndef __PACKETS_GREEN_H
#define __PACKETS_GREEN_H

struct GreenPacket
{
	u8 type;
	u32 time;
	i16 x, y, green, pid;
};


#endif

