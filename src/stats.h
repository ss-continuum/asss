
/* dist: public */

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

	void (*IncrementStat)(Player *p, int stat, int amount);
	/* increments a particular statistic in _all_ intervals */

	void (*StartTimer)(Player *p, int stat);
	void (*StopTimer)(Player *p, int stat);
	/* "timer" stats can be managed just like other stats, using
	 * IncrementStat, or you can use these functions, which take care of
	 * tracking the start time and updating the database periodically. */

	void (*SetStat)(Player *p, int stat, int interval, int value);
	/* sets a statistic to a given value */

	int (*GetStat)(Player *p, int stat, int interval);
	/* gets the value of one statistic */

	void (*SendUpdates)(void);
	/* sends out score updates for everyone that needs to be updated */
} Istats;

#endif

