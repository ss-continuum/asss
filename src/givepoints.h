
#ifndef __GIVEPOINTS_H
#define __GIVEPOINTS_H

/* Igivepoints - the interface to all of the point allocation rules
 *
 * The functions in this interface will be called when the appropriate
 * events occur. They are responsible for calling the correct functions
 * in the 'stats' module to award the correct number of points.
 *
 */

typedef struct Igivepoints
{
	/* called when someone kills someone else */
	void (*Kill)(int arena, int killer, int killed, int bounty, int flags);

	/* called when the periodic flag timer goes off */
	void (*Periodic)(int arena);

	/* called when a flag game is won */
	void (*FlagGame)(int arena, int freq);

	/* called when a goal is scored */
	void (*Goal)(int arena, int scorer, int balled);
} Igivepoints;

#endif

