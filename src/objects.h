
#ifndef __OBJECTS_H
#define __OBJECTS_H

/* Iobjects
 * this module will handle all object related packets
 */


#define I_OBJECTS "objects-1"

typedef struct Iobjects
{
	INTERFACE_HEAD_DECL

	void (*ToggleArenaMultiObjects)(int arena, short *objs, char *ons, int size);
	/* arpc: void(int, short*, char*, int) */

	void (*TogglePidSetMultiObjects)(int *pidset, short *objs, char *ons, int size);
	/* arpc: void(int*, short*, char*, int) */

	void (*ToggleMultiObjects)(int pid, short *objs, char *ons, int size);
	/* arpc: void(int, short*, char*, int) */

	void (*ToggleArenaObject)(int arena, short obj, char on);
	/* arpc: void(int, short, char) */

	void (*ToggleObject)(int pid, short obj, char on);
	/* arpc: void(int, short, char) */
} Iobjects;


#endif

