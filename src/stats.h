
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
	STAT_KPOINTS = 0,
	STAT_FPOINTS = 1,
	STAT_KILLS   = 2,
	STAT_DEATHS  = 3,
	/* this is the highest-numbered stat */
	STAT_MAX     = 15
} stat_t;

typedef struct Istats
{
	INTERFACE_HEAD_DECL

	void (*IncrementStat)(int pid, int stat, int amount);
	/* arpc: void(int, int, int) */
	/* increments a particular statistic */

	void (*SendUpdates)(void);
	/* arpc: void(void) */
	/* sends out score updates for everyone that needs to be updated */
} Istats;

#endif

