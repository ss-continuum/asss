
/* dist: public */

#ifndef __FLAGS_H
#define __FLAGS_H

/* Iflags
 * this module will handle all flag-related network communication. it
 * implements the basic flag games. other flag games can be written as
 * extra modules.
 */

#include "settings/flaggames.h"

typedef enum
{
	FLAG_NONE,    /* the flag doesn't exist */
	FLAG_ONMAP,   /* the flag is dropped on the map */
	FLAG_CARRIED, /* the flag is being carried */
	FLAG_NEUTED   /* the flag carrier specced or left, and the flag
	               * hasn't been placed yet */
} flagstate_t;


/* called when a player picks up a flag (in turf games, this means he
 * claimed the flag) */
#define CB_FLAGPICKUP ("flagpickup")
typedef void (*FlagPickupFunc)(int arena, int pid, int fid, int oldfreq, int carried);

/* called when a player drops his flags (regular games only) */
#define CB_FLAGDROP ("flagdrop")
typedef void (*FlagDropFunc)(int arena, int pid, int count, int neut);

/* called when a flag is positioned on the map */
#define CB_FLAGPOS ("flagpos")
typedef void (*FlagPosFunc)(int arena, int fid, int x, int y, int freq);

/* called when a freq owns all the flags in an arena (in regular games
 * only) */
#define CB_FLAGWIN ("flagwin")
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
	/* the number of flags currently in play */
	struct FlagData *flags;
	/* points to an array of at least flagcount structs */
};


#define I_FLAGS "flags-2"

typedef struct Iflags
{
	INTERFACE_HEAD_DECL

	void (*MoveFlag)(int arena, int fid, int x, int y, int freq);
	/* moves the specified flag to the specified coordinates */

	void (*FlagVictory)(int arena, int freq, int points);
	/* ends the flag game (freq=-1 to reset flags with no winner) */

	int (*GetCarriedFlags)(int pid);
	/* a utility function to get the number of flags carried by a player */

	int (*GetFreqFlags)(int arena, int freq);
	/* a utility function to get the number of flags owned by a freq */

	void (*LockFlagStatus)(int arena);
	void (*UnlockFlagStatus)(int arena);
	/* since the following array is global data, access must be
	 * controlled by a mutex. */

	struct ArenaFlagData *flagdata; /* indexed by arena */
} Iflags;


#endif

