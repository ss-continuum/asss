

/* ArenaActionFunc(int action, int arena) returns 1 on success and 0 on
 * failure
 */

typedef int (*ArenaActionFunc)(int, int);

#define AA_LOAD 1
#define AA_UNLOAD 2

#define CALLBACK_ARENAACTION ("arenaaction")


