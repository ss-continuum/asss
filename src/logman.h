
#ifndef __LOGMAN_H
#define __LOGMAN_H

/*
 * Ilogman - manages logging
 *
 * logging method modules add themselves with AddLog and RemoveLog.
 *
 * Log itself is used to add a line to the server log. it accepts
 * a variable number of parameters, so you can use it like printf and
 * save yourself a call to sprintf and a line. the levels mean something
 * like this:
 *
 * LOG_URGENT - sysops need to see this immediately. also for stuff like
 * server starting up and shutting down.
 * LOG_IMPORTANT - rare, important events. security stuff that isn't
 * urgent
 * LOG_INFO - basic info: players entering/leaving, arenas
 * created/destroyed.
 * LOG_USELESSINFO - basic info that is too common and fills up the logs
 * making it harder to see the imporant stuff. kills, goals, flag
 * victories.
 * LOG_DEBUG - debugging info. should probably be commented out of
 * source code when running on real servers
 *
 * LOG_BADDATA - use this one when you recieve bad data (wrong packet
 * lengths, values out of range, impossible situations)
 * LOG_ERROR - use this for really bad errors (config file not found)
 * 
 */


/* urgency levels */
#define LOG_NONE 0
#define LOG_URGENT 1
#define LOG_IMPORTANT 2
#define LOG_INFO 3
#define LOG_USELESSINFO 4
#define LOG_DEBUG 5

/* descriptive levels */
#define LOG_BADDATA LOG_IMPORTANT
#define LOG_ERROR LOG_URGENT


typedef void (*LogFunc)(int level, char *message);


typedef struct Ilogman
{
	void (*AddLog)(LogFunc func);
	void (*RemoveLog)(LogFunc func);
	void (*Log)(int level, char *format, ...);
} Ilogman;


#endif

