
/* dist: public */

#ifndef __MAINLOOP_H
#define __MAINLOOP_H


/*
 * Imainloop - main loop of program, timers
 *
 * Add/RemoveMainLoop are self-explanatory.
 *
 * SetTimer is used to set a timer that will get called periodically.
 * timer functions should return true if they want to be called again,
 * false if their work is done.
 *
 * you can specify both an initial delay to the first time the timer
 * will be called, and then a regular interval. of course, they can be
 * the same.
 *
 * ClearTimer should be used when your module is unloading to make sure
 * that your functions aren't called after they are unloaded from
 * memory. don't forget to call it for all your timers or things will
 * crash.
 *
 * RunLoop is obviously used only once by main()
 *
 * Quit can be called by any module and will shutdown the server. it
 * takes an exit code to return to the os.
 *
 */


typedef int (*TimerFunc)(void *param);
typedef void (*CleanupFunc)(void *param);
typedef void (*MainLoopFunc)(void);

#define CB_MAINLOOP ("mainloop")


#define I_MAINLOOP "mainloop-2"

typedef struct Imainloop
{
	INTERFACE_HEAD_DECL

	void (*SetTimer)(TimerFunc func, int initialdelay, int interval,
			void *param, void *key);
	/* key is a number that can be used to selectively clear timers.
	 * setting key to an arena id makes a lot of sense. */

	void (*ClearTimer)(TimerFunc func, void *key);
	/* clears all timers using the function with the given key. calling
	 * this with a key of NULL means clear all timers using that function,
	 * regardless of key */

	void (*CleanupTimer)(TimerFunc func, void *key, CleanupFunc cleanup);
	/* does the same as ClearTimer, but calls a cleanup function with
	 * the timer's parameter after it removes it */

	int (*RunLoop)(void);
	void (*Quit)(int code);
} Imainloop;


#endif

