
#ifndef __PACKETS_KILL_H
#define __PACKETS_KILL_H

struct KillPacket
{
	u8 type, green;
	i16 killer, killed, bounty, flags;
};

#endif

