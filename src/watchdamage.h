
/* dist: public */

#ifndef __WATCHDAMAGE_H
#define __WATCHDAMAGE_H

#include "packets/watchdamage.h"

/* called when get player damage */
#define CB_PLAYERDAMAGE ("playerdamage")
typedef void (*PlayerDamage)(Arena *arena, int pid, struct S2CWatchDamage *damage);


#define I_WATCHDAMAGE "watchdamage-1"

typedef struct Iwatchdamage
{
	INTERFACE_HEAD_DECL

	int (*AddWatch)(int pid, int target);
	/* adds a watch from pid on target */

	void (*RemoveWatch)(int pid, int target);
	/* removes a watch from pid on target */

	void (*ClearWatch)(int pid);
	/* removes watches on pid, both to and from, including modules */

	void (*ModuleWatch)(int pid, int on);
	/* toggles if a module wants to watch pid */

	int (*WatchCount)(int pid);
	/* tells how many are watching this pid, including modules */
} Iwatchdamage;

#endif

