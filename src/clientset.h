
/* dist: public */

#ifndef __CLIENTSET_H
#define __CLIENTSET_H

/* Iclientset
 *
 * this is the interface to the module that manages the client-side
 * settings. it loads them from disk when the arena is loaded and when
 * the config files change change. arenaman calls SendClientSettings as
 * part of the arena response procedure.
 */

typedef u32 override_key_t;

#define I_CLIENTSET "clientset-4"

typedef struct Iclientset
{
	INTERFACE_HEAD_DECL

	void (*SendClientSettings)(Player *p);

	u32 (*GetChecksum)(Player *p, u32 key);

	int (*GetRandomPrize)(Arena *arena);

	override_key_t (*GetOverrideKey)(const char *section, const char *key);
	/* zero return means failure */

	void (*ArenaOverride)(Arena *arena, override_key_t key, i32 val);
	void (*ArenaUnoverride)(Arena *arena, override_key_t key);

	void (*PlayerOverride)(Player *p, override_key_t key, i32 val);
	void (*PlayerUnoverride)(Player *p, override_key_t key);
} Iclientset;


#endif

