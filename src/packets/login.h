
#ifndef __PACKETS_LOGIN_H
#define __PACKETS_LOGIN_H

/* login.h - player login packet */


struct LoginPacket
{
	u8 type;
	u8 flags;
	char name[32];
	char password[32];
	u32 D1;
	i8 blah;
	u32 permid;
	i16 cversion;
	i32 field444,field555;
	u32 D2;
	i8 blah2[12];
	byte contid[64]; /* cont only */
};

#define LEN_LOGINPACKET_VIE (sizeof(struct LoginPacket) - 64)
#define LEN_LOGINPACKET_CONT sizeof(struct LoginPacket)


#endif

