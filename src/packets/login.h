
#ifndef __PACKETS_LOGON_H
#define __PACKETS_LOGON_H

/* logon.h - player logon packet */


struct LogonPacket
{
	i8 type;
	i8 flags;
	char name[32];
	char password[32];
	u32 D1;
	i8 blah;
	u32 permid;
	i16 cversion;
	i32 field444,field555;
	u32 D2;
	i8 blah2[12];
};

#endif

