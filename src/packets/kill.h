
#ifndef __PACKETS_KILL_H
#define __PACKETS_KILL_H

struct KillPacket
{
	i8 type,unknown;
	i16 killer,killed,bounty,flags;
};

#endif

