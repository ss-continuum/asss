
/* dist: public */

#ifndef __PACKETS_TYPES_H
#define __PACKETS_TYPES_H


/* S2C PACKET TYPES */
#define S2C_ZERO 0x00
#define S2C_WHOAMI 0x01
#define S2C_ENTERINGARENA 0x02
#define S2C_PLAYERENTERING 0x03
#define S2C_PLAYERLEAVING 0x04
#define S2C_WEAPON 0x05
#define S2C_KILL 0x06
#define S2C_CHAT 0x07
#define S2C_GREEN 0x08
#define S2C_SCOREUPDATE 0x09
#define S2C_LOGINRESPONSE 0x0A
#define S2C_SOCCERGOAL 0x0B
/* compressed .wav, with extra data */
#define S2C_WAVEFILE 0x0C
#define S2C_FREQCHANGE 0x0D
#define S2C_TURRET 0x0E
#define S2C_SETTINGS 0x0F
#define S2C_INCOMINGFILE 0x10
/* missing 11 */
#define S2C_FLAGLOC 0x12
#define S2C_FLAGPICKUP 0x13
#define S2C_FLAGRESET 0x14
#define S2C_TURRETKICKOFF 0x15
#define S2C_FLAGDROP 0x16
/* missing 17 */
#define S2C_SECURITY 0x18
#define S2C_REQUESTFORFILE 0x19
#define S2C_TIMEDGAME 0x1A
/* just 1 byte, tells client they need to reset their ship */
#define S2C_SHIPRESET 0x1B
/* two bytes, if byte two is true, client needs to send their item info in
 * position packets */
#define S2C_EXTRADATA 0x1C
#define S2C_SHIPCHANGE 0x1D
#define S2C_BANNERTOGGLE 0x1E
#define S2C_BANNER 0x1F
#define S2C_PRIZERECV 0x20
#define S2C_BRICK 0x21
#define S2C_TURFFLAGS 0x22
#define S2C_PERIODICREWARD 0x23
/* complex speed stats */
#define S2C_SPEED 0x24
/* two bytes, if byte two is true, you can use UFO if you want to */
#define S2C_UFO 0x25
/* missing 26 */
#define S2C_KEEPALIVE 0x27
#define S2C_POSITION 0x28
#define S2C_MAPFILENAME 0x29
#define S2C_MAPDATA 0x2A
#define S2C_SETKOTHTIMER 0x2B
#define S2C_KOTH 0x2C
/* missing 2D */
#define S2C_BALL 0x2E
#define S2C_ARENA 0x2F
/* vie's old method of showing ads */
#define S2C_ADBANNER 0x30
/* vie sent it after a good login, only with billing. */
#define S2C_LOGINOK 0x31
/* u8 type - ui16 x tile coords - ui16 y tile coords */
#define S2C_WARPTO 0x32
/* missing 33 34 */
/* u8 type - unlimited number of ui16 with obj id (if & 0xF000, means
 * turning off) */
#define S2C_TOGGLEOBJ 0x35
/* that ugly struct in mapobj.h that doesn't work yet */
#define S2C_RECVOBJECT 0x36
/* two bytes, if byte two is true, client should send damage info */
#define S2C_TOGGLEDAMAGE 0x37
/* complex, the info used from a *watchdamage */
#define S2C_DAMAGE 0x38


/* C2S PACKET TYPES */
#define C2S_GOTOARENA 0x01
#define C2S_LEAVING 0x02
#define C2S_POSITION 0x03
/* missing 04 */
#define C2S_DIE 0x05
#define C2S_CHAT 0x06
#define C2S_GREEN 0x07
#define C2S_SPECREQUEST 0x08
#define C2S_LOGIN 0x09
/* ugly packet in mapobj.h */
#define C2S_OBJECTMOVER 0x0A
/* 1 byte, client wants the server's update for client */
#define C2S_EXEREQUEST 0x0B
#define C2S_MAPREQUEST 0x0C
#define C2S_NEWSREQUEST 0x0D
/* sending a .wav to another client */
#define C2S_WAVESEND 0x0E
#define C2S_SETFREQ 0x0F
#define C2S_ATTACHTO 0x10
/* missing 12 */
#define C2S_PICKUPFLAG 0x13
#define C2S_TURRETKICKOFF 0x14
#define C2S_DROPFLAGS 0x15
/* uploading a file to server */
#define C2S_UPLOADFILE 0x16
#define C2S_REGDATA 0x17
#define C2S_SETSHIP 0x18
/* sending new banner */
#define C2S_BANNER 0x19
#define C2S_SECURITYRESPONSE 0x1A
#define C2S_CHECKSUMMISMATCH 0x1B
#define C2S_BRICK 0x1C
#define C2S_SETTINGCHANGE 0x1D
#define C2S_KOTHEXPIRED 0x1E
#define C2S_SHOOTBALL 0x1F
#define C2S_PICKUPBALL 0x20
#define C2S_GOAL 0x21
/* missing 22 23 */
/* Had 22 logged once: 22 59 B8 13 03 3A 9E 96 60 48 C9 1C 28 0E 00 00 00 00 */
#define C2S_CONTLOGIN 0x24
#define C2S_DAMAGE 0x32



#define S2B_KEEPALIVE 0x01
#define S2B_LOGIN 0x02
#define S2B_LOGOFF 0x03
#define S2B_PLAYERLOGIN 0x04
#define S2B_PLAYERLEAVING 0x05
/* missing 06 */
#define S2B_PRIVATEMSG 0x07
/* missing 08 09 0A 0B 0C */
/* REGDATA is just forwarding C2S_REGDATA to billing */
#define S2B_REGDATA 0x0D
#define S2B_LOGMESSAGE 0x0E
#define S2B_WARNING 0x0F
#define S2B_BANNER 0x10
#define S2B_STATUS 0x11
#define S2B_SZONEMSG 0x12
#define S2B_COMMAND 0x13
#define S2B_CHATMSG 0x14


#define B2S_PLAYERDATA 0x01
#define B2S_SHUTDOWN 0x02
#define B2S_ZONEMESSAGE 0x03
#define B2S_RECYCLE 0x04
/* missing 05 06 07 */
#define B2S_KICKUSER 0x08
#define B2S_SINGLEMESSAGE 0x09
#define B2S_CHATMSG 0x0A


#endif

