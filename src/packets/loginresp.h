
#ifndef __PACKETS_LOGINRESP_H
#define __PACKETS_LOGINRESP_H

/* loginresp.h - login repsonse packet */


struct LoginResponse
{
	i8 type;
	i8 code;
	i32 serverversion,blah,exechecksum;
	i8 blah2[2];
	i32 demodata, blah3;
	i32 newschecksum;
	i8 blah4[8];
};


#endif

