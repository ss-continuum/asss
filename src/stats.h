
#ifndef __STATS_H
#define __STATS_H

/* Istats - the statistics/scores manager
 *
 * This module has functions for managing simple scores and statistics.
 *
 */

typedef enum stat_t
{
	/* these four correspond to the standard subspace statistics */
	STAT_KILL_POINTS = 0,
	STAT_FLAG_POINTS = 1,
	STAT_KILLS   = 2,
	STAT_DEATHS  = 3,
	/* this is the highest-numbered stat */
	STAT_MAX     = 15
} stat_t;


#define I_STATS "stats-1"

typedef struct Istats
{
	INTERFACE_HEAD_DECL

	void (*IncrementStat)(int pid, int stat, int amount);
	/* arpc: void(int, int, int) */
	/* increments a particular statistic */

#if 0
	void (*SetStat)(int pid, int stat, int value);
	/* arpc: void(int, int, int) */
	/* sets a statistic to a given value */

	int (*GetStat)(int pid, int stat);
	/* arpc: void(int, int) */
	/* gets the value of one statistic */

	void (*ResetAllStats)(int pid);
	/* arpc: void(int, int, int) */
	/* resets all stats to zero */
#endif

	void (*SendUpdates)(void);
	/* arpc: void(void) */
	/* sends out score updates for everyone that needs to be updated */
} Istats;

#endif

