
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
 * you as arena[arenaid]->config. so typically you will only need to
 * call GetStr and GetInt.
 *
 * there can also be secondary global or arena config files, specified
 * with the second parameter of OpenConfigFile. these will be used for
 * oplevels and other things.
 *
 * setting configuration values is a little complicated. it doesn't work
 * right now. it might work at some time in the future, or it might not.
 *
 */


/* some defines for maximums */

#define MAXNAMELEN 32
#define MAXKEYLEN 32
#define MAXVALUELEN 256


typedef struct ConfigFile *ConfigHandle;

#define GLOBAL ((ConfigHandle)0)


typedef struct Iconfig
{
	char * (*GetStr)(ConfigHandle ch, const char *section, const char *key);
	int (*GetInt)(ConfigHandle ch, const char *section, const char *key, int defvalue);

/*	void (*SetConfigStr)(ConfigHandle, const char *, const char *, const char *); */
/*	void (*SetConfigInt)(ConfigHandle, const char *, const char *, int); */

	ConfigHandle (*OpenConfigFile)(const char *arena, const char *name);
	void (*CloseConfigFile)(ConfigHandle ch);
} Iconfig;


#endif

