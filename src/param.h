

/* absolute maximum number of players */
#define CFG_MAX_PLAYERS 255


/* maximum arenas */
#define CFG_MAX_ARENAS 50


/* the search path for config files */
#define CFG_CONFIG_SEARCH_PATH "arenas/%a/%n:defaultarena/%n:conf/%n:%n"


/* the search path for map files */
#define CFG_MAP_SEARCH_PATH "arenas/%a/%m:defaultarena/%m:maps/%m:%m"


/* whether to log private and chat messages */
#define CFG_LOG_PRIVATE


/* the format for printing time in log files, and the maximum number of
 * characters that the time could possibly take. */
#define CFG_TIMEFORMAT "%b %d %H:%M:%S"
#define CFG_TIMEFORMATLEN 20


/* whether to include uname info in the ?version output */
#define CFG_EXTRA_VERSION_INFO

/* whether to scan the arenas directory for ?arena all */
#define CFG_DO_EXTRAARENAS


/* if this is defined and the capability mananger isn't loaded, all
 * commands will be allowed. if it's not defined, _no_ commands will be
 * allowed. that's probably not what you want, unless you're very
 * paranoid. */
#define CFG_ALLOW_ALL_IF_CAPMAN_IS_MISSING


/* this is the name of the command used to get help on other commands */
#define CFG_HELP_COMMAND "help"


/* the maximum value for Team:DesiredTeams */
#define CFG_MAX_DESIRED 10


/* maximum length of a line in a config file */
#define CFG_MAX_LINE 1024


/* maximum size of a "big packet". this limits the sizes of files
 * transferred. */
#define CFG_MAX_BIG_PACKET 524288


/* maximum length of module-defined persistant data */
#define CFG_MAX_PERSIST_LENGTH 1024


/* number of lines to hold in memory for ?lastlog, and the number of
 * characters of each line to store. */
#define CFG_LAST_LINES 100
#define CFG_LAST_LENGTH 128


/* debugging option to dump raw packets to the console */
/* #define CFG_DUMP_RAW_PACKETS */


/* debugging option to log unknown packets */
#define CFG_DUMP_UNKNOWN_PACKETS


/* relax checks on certain packet lengths. only useful for debugging. */
#define CFG_RELAX_LENGTH_CHECKS


/* the size of the ip address hash table. this _must_ be a power of two. */
#define CFG_HASHSIZE 256


/* the sampling resolution for bandwidth measurement, in centiseconds */
#define CFG_BANDWIDTH_RES 100


/* maximum paramaters for the soccer game */
#define CFG_SOCCER_MAXFREQ 8
#define CFG_SOCCER_MAXGOALS 16


