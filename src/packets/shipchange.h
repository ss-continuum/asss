
#ifndef __PACKETS_SHIPCHANGE_h
#define __PACKETS_SHIPCHANGE_h


struct ShipChangePacket
{
	u8 type, shiptype;
	i16 pnum, freq;
};

#endif

