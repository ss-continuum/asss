
#ifndef __LOG_FILE_H
#define __LOG_FILE_H


typedef struct Ilog_file
{
	void (*FlushLog)(void);
	/* flushes the current log file to disk */

	void (*ReopenLog)(void);
	/* closes and reopens the current log file */
} Ilog_file;

#endif
