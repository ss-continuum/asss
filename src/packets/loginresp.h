
#ifndef __PACKETS_LOGINRESP_H
#define __PACKETS_LOGINRESP_H

/* loginresp.h - login repsonse packet */


struct LoginResponse
{
	u8 type;
	u8 code;
	u32 serverversion;
	u32 blah;
	u32 exechecksum;
	u8 blah2[2];
	u32 demodata, blah3;
	u32 newschecksum;
	u8 blah4[8];
};


#endif

