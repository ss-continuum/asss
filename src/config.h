
/* dist: public */

#ifndef __CONFIG_H
#define __CONFIG_H

/*
 * Iconfig - configuration manager
 *
 * modules get all of their settings from here and nowhere else. there
 * are two types of configuration files: global and arena. global ones
 * apply to the whole zone, and are stored in the conf directory of the
 * zone directory. arena ones are usually stored in arenas/<arenaname>,
 * but this can be customized with the search path.
 *
 * the main global configuration file is maintained internally to this
 * moudule and you don't have to open or close it. just use GLOBAL as
 * your ConfigHandle. arena configuration files are also maintained for
 * you as arena->cfg. so typically you will only need to call GetStr and
 * GetInt.
 *
 * there can also be secondary global or arena config files, specified
 * with the second parameter of OpenConfigFile. these will be used for
 * staff lists and other things.
 *
 * setting configuration values is relatively straightforward. the info
 * parameter to SetStr and SetInt should describe who initiated the
 * change and when. this information may be written back to the
 * configuration files.
 *
 * FlushDirtyValues and CheckModifiedFiles do what they say. there's no
 * need to call them, the config module performs those actions
 * internally based on timers also.
 */


/* some defines for maximums */

#define MAXSECTIONLEN 32
#define MAXKEYLEN 32
#define MAXVALUELEN 512


/* other modules should manipulate config files through ConfigHandles */
typedef struct ConfigFile *ConfigHandle;

/* use this special ConfigHandle to refer to the global config file */
#define GLOBAL ((ConfigHandle)(-3))

/* pass functions of this type to LoadConfigFile to be notified when the
 * given file is changed. */
typedef void (*ConfigChangedFunc)(void *clos);


/* this callback is called when the global config file has been changed */
#define CB_GLOBALCONFIGCHANGED "gconfchanged"
typedef void (*GlobalConfigChangedFunc)(void);
/* pycb: void */


#define I_CONFIG "config-3"

typedef struct Iconfig
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	const char * (*GetStr)(ConfigHandle ch, const char *section, const char *key);
	/* pyint: config, string, string -> string */
	int (*GetInt)(ConfigHandle ch, const char *section, const char *key, int defvalue);
	/* pyint: config, string, string, int -> int */

	void (*SetStr)(ConfigHandle ch, const char *section, const char *key,
			const char *value, const char *info, int permanent);
	/* pyint: config, string, string, string, zstring, int -> void */
	void (*SetInt)(ConfigHandle ch, const char *section, const char *key,
			int value, const char *info, int permanent);
	/* pyint: config, string, string, int, zstring, int -> void */

	ConfigHandle (*OpenConfigFile)(const char *arena, const char *name,
			ConfigChangedFunc func, void *clos);
	/* FIXMEpyint: zstring, zstring, null, null -> config
	 * (this will leak config files in the current implementation.) */
	void (*CloseConfigFile)(ConfigHandle ch);
	void (*ReloadConfigFile)(ConfigHandle ch);
	/* pyint: config -> void */
	void (*AddRef)(ConfigHandle ch);

	void (*FlushDirtyValues)(void);
	/* pyint: void -> void */
	void (*CheckModifiedFiles)(void);
	/* pyint: void -> void */
} Iconfig;


#endif

