
/* this file holds various compile-time parameters to control the
 * server's behavior. */


/* the search path for config files */
#define CFG_CONFIG_SEARCH_PATH "arenas/%a/%n:defaultarena/%n:conf/%n:%n"


/* the search path for map files */
#define CFG_MAP_SEARCH_PATH "arenas/%a/%m:defaultarena/%m:maps/%m:%m"


/* whether to log private and chat messages */
#define CFG_LOG_PRIVATE


/* whether to disallow allow loading modules from anywhere other than ./bin/ */
/* #define CFG_RESTRICT_MODULE_PATH */


/* whether to disallow module loading after the server has been initalized */
/* #define CFG_NO_RUNTIME_LOAD */


/* whether to enable persistent chat masks. this is normally a good
 * idea. the only downside is that it introduces a dependence of chat on
 * persist (i.e. you can't enable chatting without also loading the
 * database module). */
#define CFG_PERSISTENT_CHAT_MASKS


/* the format for printing time in log files, in strftime format. this
 * one looks like "Mar 26 13:33:16". */
#define CFG_TIMEFORMAT "%b %d %H:%M:%S"


/* whether to include uname info in the ?version output */
#define CFG_EXTRA_VERSION_INFO


/* whether to scan the arenas directory for ?arena all */
#define CFG_DO_EXTRAARENAS


/* if this is defined and the capability mananger isn't loaded, all
 * commands will be allowed. if it's not defined, _no_ commands will be
 * allowed. that's probably not what you want, unless you're very
 * paranoid. */
#define CFG_ALLOW_ALL_IF_CAPMAN_IS_MISSING


/* the maximum value for Team:DesiredTeams */
#define CFG_MAX_DESIRED 10


/* maximum length of a line in a config file */
#define CFG_MAX_LINE 4096


/* maximum size of a "big packet" */
#define CFG_MAX_BIG_PACKET 65536


/* maximum length of module-defined persistent data */
#define CFG_MAX_PERSIST_LENGTH 4096


/* number of lines to hold in memory for ?lastlog, and the number of
 * characters of each line to store. */
#define CFG_LAST_LINES 100
#define CFG_LAST_LENGTH 128


/* relax checks on certain packet lengths. only useful for debugging. */
/* #define CFG_RELAX_LENGTH_CHECKS */


/* the size of the ip address hash table. this _must_ be a power of two. */
#define CFG_HASHSIZE 256


/* maximum paramaters for the soccer game */
#define CFG_SOCCER_MAXFREQ 8
#define CFG_SOCCER_MAXGOALS 16


/* number of buckets, and size of each bucket, for lag measurement. note
 * that the bucket width is in milliseconds, not ticks. */
#define CFG_LAG_BUCKETS 25
#define CFG_LAG_BUCKET_WIDTH 20


/* whether to keep a list of free links. this is an optimization that
 * will have different effects on different systems. enabling it will
 * probably decrease memory use a bit, and might make things faster or
 * slower, depending on your system and malloc implementation. */
/* #define CFG_USE_FREE_LINK_LIST */


/* whether to enable a few locks for unimportant data that may decrease
 * performance (and increase lag), but are relatively safe to leave off.
 * you might want to consider enabling this on a multiprocessor box. */
/* #define CFG_PEDANTIC_LOCKING */


/* dist: public */

