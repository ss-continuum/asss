
#ifndef __CONFIG_H
#define __CONFIG_H

/*
 * Iconfig - configuration manager
 *
 * modules get all of their settings from here and nowhere else. there
 * is a global configuration file whose settings apply to all areas and
 * then each arena has a configuration file with arena-specific
 * settings. think roughly the difference between server.ini and
 * server.cfg for VIE subgame.
 *
 * the global configuration file is maintained internally to this
 * moudule and you don't have to open or close it. just use GLOBAL as
 * your ConfigHandle. arena configuration files are also maintained for
 * you as arena[arenaid]->config. so typically you will only need to
 * call GetStr and GetInt.
 *
 * setting configuration values is a little complicated. it doesn't work
 * right now. it might work at some time in the future, or it might not.
 *
 */


/* some defines for maximums */

#define MAXNAMELEN 32
#define MAXKEYLEN 32
#define MAXVALUELEN 256


typedef void * ConfigHandle;

#define GLOBAL ((ConfigHandle)0)


typedef struct Iconfig
{
	char * (*GetStr)(ConfigHandle ch, const char *section, const char *key);
	int (*GetInt)(ConfigHandle ch, const char *section, const char *key, int defvalue);

/*	void (*SetConfigStr)(ConfigHandle, const char *, const char *, const char *); */
/*	void (*SetConfigInt)(ConfigHandle, const char *, const char *, int); */

	ConfigHandle (*OpenConfigFile)(const char *name);
	void (*CloseConfigFile)(ConfigHandle ch);
} Iconfig;


#endif

