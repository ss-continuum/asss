
#ifndef __LOGMAN_H
#define __LOGMAN_H

/*
 * Ilogman - manages logging
 *
 * Log itself is used to add a line to the server log. it accepts
 * a variable number of parameters, so you can use it like printf and
 * save yourself a call to sprintf.
 *
 * log messages should be in the form:
 * "<module> {arena} [player] did something"
 * arena or player may be left out if not applicable.
 * if a player name is not available, "[pid=123]" should be used
 * instead.
 *
 */


/* priority levels */
#define L_DRIVEL     'D'  /* really useless info */
#define L_INFO       'I'  /* informative info */
#define L_MALICIOUS  'M'  /* bad stuff from the client side */
#define L_WARN       'W'  /* something bad, but not too bad */
#define L_ERROR      'E'  /* something really really bad */


typedef void (*LogFunc)(const char *line);


#define I_LOGMAN "logman-1"

typedef struct Ilogman
{
	INTERFACE_HEAD_DECL

	void (*Log)(char level, const char *format, ...);
	/* arpc: void(char, string, etc) */


	/* utilty functions for the above */

	void (*LogA)(char level, const char *mod, int arena, const char *format, ...);
	/* arpc: void(char, string, int, string, etc) */

	void (*LogP)(char level, const char *mod, int pid, const char *format, ...);
	/* arpc: void(char, string, int, string, etc) */


	/* log modules can optionally call this function for help filtering
	 * their log messages. you should pass it the log line you recieved,
	 * and then the name of your own module (filtering will be performed
	 * based on this module name). */
	int (*FilterLog)(const char *line, const char *modname);
	/* arpc: int(string, string) */
} Ilogman;


#define CB_LOGFUNC ("log")


#endif

