
/* dist: public */

#ifndef __CLIENTSET_H
#define __CLIENTSET_H

/* Iclientset
 *
 * this is the interface to the module that manages the client-side
 * settings. it loads them from disk when the arena is loaded and when
 * Reconfigure is called. arenaman calls SendClientSettings as part of
 * the arena response procedure.
 */

typedef u32 override_key_t;

#define I_CLIENTSET "clientset-3"

typedef struct Iclientset
{
	INTERFACE_HEAD_DECL

	void (*SendClientSettings)(Player *p);

	void (*Reconfigure)(Arena *arena);

	u32 (*GetChecksum)(Arena *arena, u32 key);

	int (*GetRandomPrize)(Arena *arena);

	void (*Override)(Arena *arena, override_key_t key, i32 val);
	void (*Unoverride)(Arena *arena, override_key_t key);
	override_key_t (*GetOverrideKey)(const char *section, const char *key);
	/* zero return means failure */
} Iclientset;


#endif

