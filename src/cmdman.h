
#ifndef __CMDMAN_H
#define __CMDMAN_H

/*
 * Icmdman - manages commands and stuff
 *
 * modules register commands with this module by calling AddCommand with
 * the name of a command, and a function to be called with that command
 * is typed. the command functions take a char pointer to the params
 * (that is, everything following the command on the typed line), the
 * pid who typed it (or PID_INTERNAL for an internal server-generated
 * command), and the target pid for private message commands (or
 * TARGET_ARENA for commands typed in pub messages, or TARGET_FREQ for
 * commands typed in team messages)
 *
 * note that the command itself is not passed to the command handler.
 * this means you have to register a separate function for each command
 * (unless of course you want them to do the same thing)
 *
 * commands can be removed in the same way by specifying the name and
 * function (the name must be specified again because they're stored in
 * a hash table)
 *
 * the Command function can be used to process a command. the chat
 * module uses that function to process all typed commands. a module can
 * also give commands to be processed as if they were typed in by a
 * sysop. for example, game type modules can call Command("spec",
 * PID_INTERNAL, pid) to spec a player without worrying about how
 * speccing is carried out.
 *
 * one strange feature: there is a default command function which gets
 * called when there is no matching command. this is used by the billing
 * server core module to send commands to the biller if the server
 * doesn't know about them. this is set by specifying NULL for the
 * command name. but no one other than the billing module should ever
 * have to use this.
 *
 * a final note: there is no difference between ? commands and *
 * commands! any prior distinction was artificial and i don't like
 * artificial distinctions.
 *
 */


typedef void (*CommandFunc)(const char *params, int pid, int target);


#define I_CMDMAN "cmdman-1"

typedef struct Icmdman
{
	INTERFACE_HEAD_DECL

	void (*AddCommand)(const char *cmdname, CommandFunc func);
	/* arpc: void(string, CommandFunc callback) */
	void (*RemoveCommand)(const char *cmdname, CommandFunc func);
	/* arpc: void(string, CommandFunc callback) */
	void (*Command)(const char *typedline, int pid, int target);
	/* arpc: void(string, int, int) */
} Icmdman;

#endif

