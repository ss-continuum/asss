
#ifndef __PACKETS_GOARENA_H
#define __PACKETS_GOARENA_H

/* goarena.h - the ?go arena change request packet */

struct GoArenaPacket
{
	u8 type;
	i8 shiptype;
	i16 wavmsg;
	i16 xres,yres,arenatype;
	char arenaname[16];
};

#endif

