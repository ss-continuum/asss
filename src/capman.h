
#ifndef __CAPMAN_H
#define __CAPMAN_H

/* Icapman
 *
 * manages capabilities and authority for all players. other modules
 * should query this to discover if a player has a certain authority.
 *
 * capabilities are named in the following way:
 *
 * if a player has the capabilty "cmd_<some command>", he can use that
 * command "untargetted" (that is, typed as a public message).
 *
 * if a player has the capability "privcmd_<some command>", he can use
 * that command directed at a player or freq (private or team messages).
 *
 * other capabilites (e.g., "seeprivarenas") don't follow any special
 * naming convention.
 */


/* this interface is what modules should use to query for capabilities */

#define I_CAPMAN "capman-2"

typedef struct Icapman
{
	INTERFACE_HEAD_DECL

	int (*HasCapability)(int pid, const char *cap);
	/* returns true if the given player has the given capability. */

	int (*HasCapabilityByName)(const char *name, const char *cap);
	/* same as HasCapability, but intented to be used in strange places
	 * like before the player has logged in yet */
} Icapman;


/* this interface should be used by very few places, because it might
 * not be available when using alternative capability managers. */
#define I_GROUPMAN "groupman-1"

typedef struct Igroupman
{
	INTERFACE_HEAD_DECL

	const char *(*GetGroup)(int pid);
	void (*SetPermGroup)(int pid, const char *group, int global, const char *info);
	void (*SetTempGroup)(int pid, const char *group);

	int (*CheckGroupPassword)(const char *group, const char *pwd);
	/* true if the password is correct */
} Igroupman;

#define MAXGROUPLEN 32


#endif

