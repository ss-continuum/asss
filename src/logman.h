
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


typedef void (*LogFunc)(char level, char *message);


typedef struct Ilogman
{
	void (*Log)(char level, char *format, ...);
} Ilogman;


#define CALLBACK_LOGFUNC ("imalogger")


#endif

