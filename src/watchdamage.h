
/* dist: public */

#ifndef __WATCHDAMAGE_H
#define __WATCHDAMAGE_H

#include "packets/watchdamage.h"

/* called when get player damage */
#define CB_PLAYERDAMAGE ("playerdamage")
typedef void (*PlayerDamage)(Arena *arena, Player *p, struct S2CWatchDamage *damage);


#define I_WATCHDAMAGE "watchdamage-1"

typedef struct Iwatchdamage
{
	INTERFACE_HEAD_DECL

	int (*AddWatch)(Player *p, Player *target);
	/* adds a watch from pid on target */

	void (*RemoveWatch)(Player *p, Player *target);
	/* removes a watch from pid on target */

	void (*ClearWatch)(Player *p, int himtoo);
	/* removes watches on pid, both to and from, including modules */

	void (*ModuleWatch)(Player *p, int on);
	/* toggles if a module wants to watch pid */

	int (*WatchCount)(Player *p);
	/* tells how many are watching this pid, including modules */
} Iwatchdamage;

#endif

