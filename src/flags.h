
#ifndef __FLAGS_H
#define __FLAGS_H

/* Iflags
 * this module will handle all flag-related network communication. it
 * implements the basic flag games. other flag games can be written as
 * extra modules.
 */


#define MAXFLAGS 256

#include "settings/flaggames.h"

typedef enum
{
	FLAG_NONE,    /* the flag doesn't exist */
	FLAG_ONMAP,   /* the flag is dropped on the map */
	FLAG_CARRIED, /* the flag is being carried */
	FLAG_NEUTED   /* the flag carrier specced or left, and the flag
	               * hasn't been placed yet */
} flagstate_t;


/* called when a player picks up a flag (in regular AND turf games) */
#define CALLBACK_FLAGPICKUP ("flagpickup")
typedef void (*FlagPickupFunc)(int arena, int pid, int fid, int oldfreq);

/* called when a player drops his flags (regular games only) */
#define CALLBACK_FLAGDROP ("flagdrop")
typedef void (*FlagDropFunc)(int arena, int pid);

/* called when a flag is positioned on the map */
#define CALLBACK_FLAGPOS ("flagpos")
typedef void (*FlagPosFunc)(int arena, int fid, int x, int y, int freq);

/* called when a freq owns all the flags in an arena */
#define CALLBACK_FLAGWIN ("flagwin")
typedef void (*FlagWinFunc)(int arena, int freq);


struct FlagData
{
	flagstate_t state; /* the state of this flag */
	int x, y; /* the coordinates of the flag */
	int freq; /* the freq owning the flag, or -1 if neutral */
	int carrier; /* the pid carrying the flag, or -1 if down */
};

struct ArenaFlagData
{
	int flagcount;
	struct FlagData flags[MAXFLAGS];
};


typedef struct Iflags
{
	void (*MoveFlag)(int arena, int fid, int x, int y, int freq);
	/* moves the specified flag to the specified coordinates */

	void (*FlagVictory)(int arena, int freq, int points);
	/* ends the flag game (freq=-1 to reset flags with no winner) */

	void (*LockFlagStatus)(int arena);
	void (*UnlockFlagStatus)(int arena);
	/* since the following array is global data, access must be
	 * controlled by a mutex. */

	struct ArenaFlagData *flagdata; /* indexed by arena */
} Iflags;


#endif

