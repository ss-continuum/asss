

/* elder daemon */

#define ELDERDPORT (8313)

/* messages can be up to 16kb */
#define MAX_MESSAGE_SIZE (16384)


/* asss->extension messages */

#define A2E_NULL              0
#define A2E_SHUTDOWN          1
#define A2E_EVALSTRING        2
#define A2E_PLAYERDATA        3
#define A2E_PLAYERLIST        4
#define A2E_ARENADATA         5
#define A2E_SETTING           6


/* extension->asss messages */

#define E2A_NULL              0
#define E2A_SHUTTINGDOWN      1
#define E2A_SENDMESSAGE       2
#define E2A_GETPLAYERDATA     3
#define E2A_GETPLAYERLIST     4
#define E2A_FINDPLAYER        5
#define E2A_GETSETTING        6
#define E2A_SETSETTING        7
#define E2A_INSTALLCALLBACK   8
#define E2A_RUNCOMMAND        9


/* structs of all sizes. asss->extension first */

#pragma pack(1)

struct data_a2e_null
{
	int type;
};

struct data_a2e_shutdown
{
	int type;
};

struct data_a2e_evalstring
{
	int type;
	int pid; /* pid that requested the evaluation, or -1 for server-generated */
	char string[1];
};

#include "packets/sizes.h"
#include "packets/pdata.h"

struct data_a2e_playerdata
{
	int type;
	int pid;
	PlayerData data;
};

struct data_a2e_playerlist
{
	int type;
	int pidcount;
	int pid[1]; /* contains pidcount real pids AND terminated by -1 */
};

struct data_a2e_arenadata
{
	int type;
	/* blank for now, will have ArenaData */
};

struct data_a2e_setting
{
	int type;
	char data[1];
};



/* structs for asss->extension */

struct data_e2a_null
{
	int type;
};

struct data_e2a_shuttingdown
{
	int type;
};

struct data_e2a_sendmessage
{
	int type;
	int pid;
	char message[1];
};

struct data_e2a_getplayerdata
{
	int type;
	int pid;
};

struct data_e2a_getplayerlist
{
	int type;
	int arena; /* -1 = all arenas, else = specific arena */
	int freq; /* -1 = all freqs, else = specific freq */
	int shiptype; /* -1 = all shiptypes, else = specific ones */
};

struct data_e2a_findplayer
{
	int type;
	char name[1];
};

#include "config.h"

struct data_e2a_getsetting
{
	int type;
	char section[MAXNAMELEN];
	char key[MAXKEYLEN];
};

struct data_e2a_setsetting
{
	int type;
	char section[MAXNAMELEN];
	char key[MAXKEYLEN];
	char data[MAXVALUELEN];
};

struct data_e2a_installcallback
{
	int type;
	int callbacktype; /* see below */
	int arena; /* -1 for all, or specific */
	int param1, param2, param3; /* callbacktype dependant */
	char expression[1];
};

#include "cmdman.h"

struct data_e2a_runcommand
{
	int type;
	int target; /* target pid for command. source pid will always be PID_INTERNAL */
	char command[1]; /* make sure that's without the * or ? */
};



/* callback types and signatures */

#define CB_PERIODIC           1
/* perioding timer - pretty bad resolution, don't rely on it
 * param1 = seconds for timer
 * expected signature: (lambda (arena) ...)
 */

#define CB_KILL               2
/* called on every kill
 * no params
 * expected signature: (lambda (killer killed bounty flags) ...)
 */


/* more to come */


