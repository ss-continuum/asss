
/* dist: public */

#ifndef __CMDMAN_H
#define __CMDMAN_H

/*
 * Icmdman - manages commands
 *
 * modules register commands with this module by calling AddCommand with
 * the name of a command, and a function to be called with that command
 * is typed. the command functions take a char pointer to the params
 * (that is, everything following the command on the typed line), the
 * player who typed it, and a target structure describing how the
 * command was delivered.
 *
 * the alternate interface, AddCommand2, allows you to specify a
 * function that will also get the command name typed. this is useful
 * for the billing module, bots, and extension language modules. it also
 * lets you register arena-specific commands.
 *
 * commands can be removed in the same way by specifying the name and
 * function (the name must be specified again because they're stored in
 * a hash table)
 *
 * the Command function can be used to process a command. the chat
 * module uses that function to process all typed commands.
 *
 * if the first character of a line passed to Command is a backslash,
 * command handlers in asss will be skipped and the command will be
 * passed directly to the default handler (usually a billing server).
 *
 * a final note: there is no difference between ? commands and *
 * commands! any prior distinction was artificial and i don't like
 * artificial distinctions.
 *
 */


typedef void (*CommandFunc)(const char *params, Player *p, const Target *target);
typedef void (*CommandFunc2)(const char *command, const char *params,
		Player *p, const Target *target);


typedef const char *helptext_t;

#define I_CMDMAN "cmdman-6"

typedef struct Icmdman
{
	INTERFACE_HEAD_DECL

	void (*AddCommand)(const char *cmdname, CommandFunc func, helptext_t ht);
	void (*AddCommand2)(const char *cmdname, CommandFunc2 func,
			Arena *arena, helptext_t ht);
	void (*RemoveCommand)(const char *cmdname, CommandFunc func);
	void (*RemoveCommand2)(const char *cmdname, CommandFunc2 func,
			Arena *arena);
	void (*Command)(const char *typedline, Player *p, const Target *target);
	helptext_t (*GetHelpText)(const char *cmdname);
} Icmdman;

#endif

