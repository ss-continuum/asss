
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
 * Quit can be called by any module and will shutdown the server
 *
 */


typedef int (*TimerFunc)(void *param);

typedef void (*MainLoopFunc)(void);

#define CB_MAINLOOP ("mainloop")


typedef struct Imainloop
{
	INTERFACE_HEAD_DECL

	void (*SetTimer)(TimerFunc func, int initialdelay, int interval, void *param);
	void (*ClearTimer)(TimerFunc func);

	void (*RunLoop)(void);
	void (*Quit)(void);
} Imainloop;


#endif

