
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
/* missing 0C */
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
/* missing 1B 1C */
#define S2C_SHIPCHANGE 0x1D
/* missing 1E */
#define S2C_BANNER 0x1F
#define S2C_PRIZERECV 0x20
#define S2C_BRICK 0x21
#define S2C_TURFFLAGS 0x22
#define S2C_PERIODICREWARD 0x23
/* missing 24 25 26 */
#define S2C_KEEPALIVE 0x27
#define S2C_POSITION 0x28
#define S2C_MAPFILENAME 0x29
#define S2C_MAPDATA 0x2A
/* missing 2B */
#define S2C_KOTH 0x2C
/* missing 2D */
#define S2C_BALL 0x2E
#define S2C_ARENA 0x2F
/*missing 30 31 */


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
/* missing 0A 0B */
#define C2S_MAPREQUEST 0x0C
#define C2S_NEWSREQUEST 0x0D
/* missing 0E */
#define C2S_SETFREQ 0x0F
#define C2S_ATTACHTO 0x10
/* missing 11 12 */
#define C2S_PICKUPFLAG 0x13
#define C2S_TURRETKICKOFF 0x14
#define C2S_DROPFLAGS 0x15
/* missing 16 17 */
#define C2S_SETSHIP 0x18
/* missing 19 */
#define C2S_SECURITYRESPONSE 0x1A
#define C2S_CHECKSUMMISMATCH 0x1B
#define C2S_BRICK 0x1C
/* missing 1D */
#define C2S_KOTHEXPIRED 0x1E
#define C2S_SHOOTBALL 0x1F
#define C2S_PICKUPBALL 0x20
#define C2S_GOAL 0x21


#define S2B_KEEPALIVE 0x01
#define S2B_LOGIN 0x02
#define S2B_LOGOFF 0x03
#define S2B_PLAYERLOGIN 0x04
#define S2B_PLAYERLEAVING 0x05
/* missing 06 */
#define S2B_PRIVATEMSG 0x07
/* missing 08 09 0A 0B 0C 0D 0E */
#define S2B_WARNING 0x0F
#define S2B_BANNER 0x10
/* missing 10 11 */
#define S2B_SZONEMSG 0x12
#define S2B_COMMAND 0x13
#define S2B_CHATMSG 0x14
 
 
#define B2S_PLAYERDATA 0x01
/* missing 02 */
#define B2S_MESSAGE 0x03
/* missing 04 05 06 07 08 */
#define B2S_SINGLEMESSAGE 0x09
#define B2S_CHATMSG 0x0A


#endif

