
/* dist: public */

#ifndef __OBJECTS_H
#define __OBJECTS_H

/* Iobjects
 * this module will handle all object related packets
 */


#define I_OBJECTS "objects-1"

typedef struct Iobjects
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*ToggleArenaMultiObjects)(Arena *arena, short *objs, char *ons, int size);
	void (*TogglePidSetMultiObjects)(LinkedList *set, short *objs, char *ons, int size);
	void (*ToggleMultiObjects)(Player *p, short *objs, char *ons, int size);
	void (*ToggleArenaObject)(Arena *arena, short obj, char on);
	/* pyint: arena, int, int -> void */
	void (*ToggleObject)(Player *p, short obj, char on);
	/* pyint: player, int, int -> void */
} Iobjects;


#endif

