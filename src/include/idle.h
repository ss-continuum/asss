
/* dist: public */

#ifndef __IDLE_H
#define __IDLE_H

/* keeps track of idle time for players */

#define I_IDLE "idle-2"

typedef struct Iidle
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	int (*GetIdle)(Player *p);
	/* pyint: player -> int */
	void (*ResetIdle)(Player *p);
	/* pyint: player -> void */
} Iidle;

#endif

