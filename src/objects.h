
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

	void (*ToggleArenaMultiObjects)(Arena *arena, short *objs, char *ons, int size);
	void (*TogglePidSetMultiObjects)(int *pidset, short *objs, char *ons, int size);
	void (*ToggleMultiObjects)(int pid, short *objs, char *ons, int size);
	void (*ToggleArenaObject)(Arena *arena, short obj, char on);
	void (*ToggleObject)(int pid, short obj, char on);
} Iobjects;


#endif

