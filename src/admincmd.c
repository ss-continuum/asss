
/* dist: public */

#ifndef WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "filetrans.h"
#include "log_file.h"


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *lm;
local Icmdman *cmd;
local Icapman *capman;
local Ilog_file *logfile;
local Ifiletrans *filetrans;
local Imodman *mm;


local helptext_t admlogfile_help =
"Targets: none\n"
"Args: {flush} or {reopen}\n"
"Administers the log file that the server keeps. There are two possible\n"
"subcommands: {flush} flushes the log file to disk (in preparation for\n"
"copying it, for example), and {reopen} tells the server to close and\n"
"re-open the log file (to rotate the log while the server is running).\n";

local void Cadmlogfile(const char *params, Player *p, const Target *target)
{
	if (!strcasecmp(params, "flush"))
		logfile->FlushLog();
	else if (!strcasecmp(params, "reopen"))
		logfile->ReopenLog();
}



local helptext_t getfile_help =
"Targets: none\n"
"Args: <filename>\n"
"Transfers the specified file from the server to the client.\n"
"The filename should include the full relative path from the server's\n"
"base directory.\n";

local void Cgetfile(const char *params, Player *p, const Target *target)
{
	const char *t1 = strrchr(params, '/');
	const char *t2 = strrchr(params, '\\');
	if (t2 > t1) t1 = t2;
	t1 = t1 ? t1 + 1 : params;

	if (params[0] == '/' || strstr(params, ".."))
		lm->LogP(L_MALICIOUS, "playercmd", p, "attempted ?getfile with bad path: '%s'", params);
	else
		filetrans->SendFile(p, params, t1, 0);
}


typedef struct upload_t
{
	Player *p;
	int unzip;
	char serverpath[1];
} upload_t;


local void uploaded(const char *fname, void *clos)
{
	upload_t *u = clos;
	int r;

	if (fname && u->unzip)
	{
		/* unzip it to the destination directory */
#ifndef WIN32 /* should use popen, since more portable */
		r = fork();
		if (r == 0)
		{
			/* in child, perform unzip */
			close(0); close(1); close(2);
			r = fork();
			if (r == 0)
			{
				/* in child of child */
				/* -qq to be quieter
				 * -a to auto-convert line endings between dos and unix
				 * -o to overwrite all files
				 * -j to ignore paths specified in the zip file (for security)
				 * -d to specify the destination directory */
				execlp("unzip", "unzip", "-qq", "-a", "-o", "-j",
						"-d", u->serverpath, fname, NULL);
			}
			else if (r > 0)
			{
				/* in parent of child. wait for unzip to finish, then
				 * unlink the file */
				waitpid(r, NULL, 0);
				unlink(fname);
			}
			_exit(0);
		}
		else if (r < 0)
#endif
			lm->Log(L_WARN, "<admincmd> can't fork to unzip uploaded .zip file");
	}
	else if (fname)
	{
		/* just move it to the right place */
		r = rename(fname, u->serverpath);
		if (r < 0)
		{
			lm->LogP(L_WARN, "admincmd", u->p, "couldn't rename file '%s' to '%s'",
					fname, u->serverpath);
			chat->SendMessage(u->p, "Couldn't upload file to '%s'", u->serverpath);
			remove(fname);
		}
		else
			chat->SendMessage(u->p, "File received: %s", u->serverpath);
	}

	afree(u);
}


local helptext_t putfile_help =
"Targets: none\n"
"Args: <client filename>:<server filename>\n"
"Transfers the specified file from the client to the server.\n"
"The server filename must be a full path name relative to the base\n"
"directory of the server. (Remember, servers running on unix systems\n"
"use forward slashes to separate path components.)\n";

local void Cputfile(const char *params, Player *p, const Target *target)
{
	char clientfile[256];
	const char *serverpath;

	serverpath = delimcpy(clientfile, params, 256, ':');
	if (!serverpath)
	{
		chat->SendMessage(p, "You must specify a destination path on the server. "
				"?help putfile for more information.");
	}
	else
	{
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 0;
		strcpy(u->serverpath, serverpath);

		filetrans->RequestFile(p, clientfile, uploaded, u);
	}
}


local helptext_t putzip_help =
"Targets: none\n"
"Args: <client filename>:<server directory>\n"
"Uploads the specified zip file to the server and unzips it in the\n"
"specified directory. This can be used to efficiently send a large\n"
"number of files to the server at once, while preserving directory\n"
"structure.\n";

local void Cputzip(const char *params, Player *p, const Target *target)
{
	char clientfile[256];
	const char *serverpath;

	serverpath = delimcpy(clientfile, params, 256, ':');
	if (!serverpath)
	{
		chat->SendMessage(p, "You must specify a destination directory on the server. "
				"?help putzip for more information.");
	}
	else
	{
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 1;
		strcpy(u->serverpath, serverpath);

		filetrans->RequestFile(p, clientfile, uploaded, u);
	}
}


local helptext_t botfeature_help =
"Targets: none\n"
"Args: [+/-{seeallposn}]\n"
"Enables or disables bot-specific features. {seeallposn} controls whether\n"
"the bot gets to see all position packets.\n";

local void Cbotfeature(const char *params, Player *p, const Target *target)
{
	char buf[64];
	const char *tmp = NULL;

	while (strsplit(params, " ,", buf, sizeof(buf), &tmp))
	{
		int on;

		if (buf[0] == '+')
			on = 1;
		else if (buf[0] == '-')
			on = 0;
		else
		{
			chat->SendMessage(p, "Bad syntax");
			continue;
		}

		if (!strcmp(buf+1, "seeallposn"))
			p->flags.see_all_posn = on;
		else
			chat->SendMessage(p, "Unknown bot feature");
	}
}



EXPORT int MM_admincmd(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		logfile = mm->GetInterface(I_LOG_FILE, ALLARENAS);
		filetrans = mm->GetInterface(I_FILETRANS, ALLARENAS);
		if (!pd || !chat || !lm || !cmd || !capman || !logfile || !filetrans)
			return MM_FAIL;

		cmd->AddCommand("admlogfile", Cadmlogfile, admlogfile_help);
		cmd->AddCommand("getfile", Cgetfile, getfile_help);
		cmd->AddCommand("putfile", Cputfile, putfile_help);
		cmd->AddCommand("putzip", Cputzip, putzip_help);
		cmd->AddCommand("botfeature", Cbotfeature, botfeature_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("admlogfile", Cadmlogfile);
		cmd->RemoveCommand("getfile", Cgetfile);
		cmd->RemoveCommand("putfile", Cputfile);
		cmd->RemoveCommand("putzip", Cputzip);
		cmd->RemoveCommand("botfeature", Cbotfeature);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(logfile);
		mm->ReleaseInterface(filetrans);
		return MM_OK;
	}
	return MM_FAIL;
}


