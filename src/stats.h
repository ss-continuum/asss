
#ifndef __STATS_H
#define __STATS_H

/* Istats - the statistics/scores manager
 *
 * This module has functions for managing simple scores and statistics.
 */


/* get the stat id codes */
#include "statcodes.h"


#define I_STATS "stats-2"

typedef struct Istats
{
	INTERFACE_HEAD_DECL

	void (*IncrementStat)(int pid, int stat, int amount);
	/* arpc: void(int, int, int) */
	/* increments a particular statistic in _all_ intervals */

	void (*SetStat)(int pid, int stat, int interval, int value);
	/* arpc: void(int, int, int, int) */
	/* sets a statistic to a given value */

	int (*GetStat)(int pid, int stat, int interval);
	/* arpc: void(int, int, int) */
	/* gets the value of one statistic */

	void (*SendUpdates)(void);
	/* arpc: void(void) */
	/* sends out score updates for everyone that needs to be updated */
} Istats;

#endif

