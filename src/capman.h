
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

#define MAXGROUPLEN 32



#define I_CAPMAN "capman-1"

typedef struct Icapman
/* arpc: interface capman remoteok callok */
{
	INTERFACE_HEAD_DECL

	int (*HasCapability)(int pid, const char *cap);
	/* arpc: int, string -> int (0) */
	/* returns true if the given player has the given capability. */

	int (*HasCapabilityByName)(const char *name, const char *cap);
	/* arpc: string, string -> int (0) */
	/* same as HasCapability, but intented to be used in strange places
	 * like before the player has logged in yet */

	const char *(*GetGroup)(int pid);
	/* arpc: int -> string (NULL) */
	void (*SetPermGroup)(int pid, const char *group, int global, const char *info);
	/* arpc: int, string -> void */
	void (*SetTempGroup)(int pid, const char *group);
	/* arpc: int, string -> void */

	/* gets/sets the group of the player as specified. these functions
	 * are dependant on one specific implementation of capabilities, and
	 * should only be used from very few places. */
} Icapman;


#endif

