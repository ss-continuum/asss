
#ifndef __DEFS_H
#define __DEFS_H

#include <linux/stddef.h>


/* do it upfront so we don't have to worry :) */
#pragma pack(1)

/* to keep most stuff local to modules */
#define local static


/* really important constants */
#define MAXPLAYERS 255
#define MAXARENA 50

#define MAXPACKET 512
#define MAXBIGPACKET 524288

#define MAXINTERFACE 256


/* interface ids, kept here to make sure they're unique */
#define I_MODMAN		0
#define I_MAINLOOP		1
#define I_CONFIG		2
#define I_NET			3
#define I_LOGMAN		4
#define I_CMDMAN		5
#define I_CHAT			6
#define I_CORE			7
#define I_AUTH			8
#define I_ASSIGNFREQ	9
#define I_BILLCORE		10
#define I_ENCRYPTBASE	90
/*#define I_FLAG */
/*#define I_BALL */
/*#define I_DATABASE */
/*#define I_BRICK */
/*#define I_BANNER */


/* action codes for module main functions */
#define MM_LOAD 1
#define MM_UNLOAD 2
#define MM_GETDEPS 3
#define MM_DESCRIBE 4


/* return values for the aforementioned */
#define MM_FAIL 1
#define MM_OK 0


/* hopefully useful exit codes */
#define ERROR_NONE 0
#define ERROR_NORMAL 5
#define ERROR_CRITICAL 10
#define ERROR_MEMORY 7


/* authentication return codes */
#define AUTH_OK				0x00
#define AUTH_UNKNOWN		0x01
#define AUTH_BADPASSWORD	0x02
#define AUTH_ARENAFULL		0x03
#define AUTH_LOCKEDOUT		0x04
#define AUTH_NOPERMISSION	0x05
#define AUTH_SPECONLY		0x06
#define AUTH_TOOMANYPOINTS	0x07
#define AUTH_TOOSLOW		0x08
#define AUTH_NOPERMISSION2	0x09
#define AUTH_NONEWCONN		0x0A
#define AUTH_BADNAME		0x0B
#define AUTH_OFFENSIVENAME	0x0C
#define AUTH_NOSCORES		0x0D
#define AUTH_SERVERBUSY		0x0E
#define AUTH_EXPONLY		0x0F
#define AUTH_ISDEMO			0x10
#define AUTH_TOOMANYDEMO	0x11
#define AUTH_NODEMO			0x12


/* weapon codes */
#define W_NULL 0
#define W_BULLET 1
#define W_BOUNCEBULLET 2
#define W_BOMB 3
#define W_PROXBOMB 4
#define W_REPEL 5
#define W_DECOY 6
#define W_BURST 7
#define W_THOR 8


/* some ship names */
#define WARBIRD 0
#define JAVELIN 1
/* ... */
#define SPEC 8


/* symbolic constant for freq assignemnt */
#define BADFREQ (-423)

/* size of 0x0F settings packet  FIXME: unnecesary */
#define SETTINGSIZE 1428


/* useful typedefs */
typedef unsigned char byte;

typedef char i8;
typedef short i16;
typedef int i32;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


#include "packets/types.h"

#include "packets/ppk.h"

#include "packets/pdata.h"

#include "packets/simple.h"

#endif

