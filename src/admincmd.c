
/* dist: public */

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "asss.h"
#include "filetrans.h"
#include "log_file.h"


/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
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
		lm->LogP(L_MALICIOUS, "admincmd", p, "attempted ?getfile with bad path: '%s'", params);
	else
		filetrans->SendFile(p, params, t1, 0);
}


typedef struct upload_t
{
	Player *p;
	int unzip;
	const char *setting;
	char serverpath[1];
} upload_t;


local void uploaded(const char *fname, void *clos)
{
	upload_t *u = clos;
	int r = 0;

	if (fname && u->unzip)
	{
		chat->SendMessage(u->p, "Zip inflated to: %s", u->serverpath);
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
		/* move it to the right place */
#ifdef WIN32
		r = unlink(u->serverpath);
#endif
		if (r >= 0)
			r = rename(fname, u->serverpath);

		if (r < 0)
		{
			lm->LogP(L_WARN, "admincmd", u->p, "couldn't rename file '%s' to '%s'",
					fname, u->serverpath);
			chat->SendMessage(u->p, "Couldn't upload file to '%s'", u->serverpath);
			remove(fname);
		}
		else
		{
			chat->SendMessage(u->p, "File received: %s", u->serverpath);
			if (u->setting && cfg)
			{
				char info[128];
				time_t tm = time(NULL);
				snprintf(info, 100, "set by %s with ?putmap on ", u->p->name);
				ctime_r(&tm, info + strlen(info));
				RemoveCRLF(info);
				cfg->SetStr(u->p->arena->cfg, u->setting, NULL,
						u->serverpath, info, TRUE);
				chat->SendMessage(u->p, "Set %s=%s", u->setting, u->serverpath);
			}
		}
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
	else if (serverpath[0] == '/' || strstr(serverpath, ".."))
	{
		lm->LogP(L_MALICIOUS, "admincmd", p, "attempted ?putfile with bad path: '%s'", params);
	}
	else
	{
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 0;
		u->setting = NULL;
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
	else if (serverpath[0] == '/' || strstr(serverpath, ".."))
	{
		lm->LogP(L_MALICIOUS, "admincmd", p, "attempted ?putzip with bad path: '%s'", params);
	}
	else
	{
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 1;
		u->setting = NULL;
		strcpy(u->serverpath, serverpath);

		filetrans->RequestFile(p, clientfile, uploaded, u);
	}
}


local helptext_t putmap_help =
"Targets: none\n"
"Args: <map file>\n"
"Transfers the specified map file from the client to the server.\n"
"The map will be placed in maps/uploads/<arenabasename>.lvl,\n"
"and the setting General:Map will be changed to the name of the\n"
"uploaded file.\n";

local void Cputmap(const char *params, Player *p, const Target *target)
{
	char serverpath[256];
	upload_t *u;

	/* make sure these exist */
	mkdir("maps", 0666);
	mkdir("maps/uploads", 0666);

	snprintf(serverpath, sizeof(serverpath),
			"maps/uploads/%s.lvl", p->arena->basename);

	u = amalloc(sizeof(*u) + strlen(serverpath));

	u->p = p;
	u->unzip = 0;
	u->setting = "General:Map";
	strcpy(u->serverpath, serverpath);

	filetrans->RequestFile(p, params, uploaded, u);
}


local helptext_t makearena_help =
"Targets: none\n"
"Args: <arena name>\n"
"FIXME\n";

local void Cmakearena(const char *params, Player *p, const Target *target)
{
	char buf[128];
	FILE *f;

	snprintf(buf, sizeof(buf), "arenas/%s", params);
	if (mkdir(buf, 0755) < 0)
	{
		char err[128];
		strerror_r(errno, err, sizeof(err));
		chat->SendMessage(p, "Error creating directory '%s': %s", buf, err);
		lm->Log(L_WARN, "<admincmd> error creating directory '%s': %s", buf, err);
	}

	snprintf(buf, sizeof(buf), "arenas/%s/arena.conf", params);
	f = fopen(buf, "w");
	if (!f)
	{
		char err[128];
		strerror_r(errno, err, sizeof(err));
		chat->SendMessage(p, "Error creating file '%s': %s", buf, err);
		lm->Log(L_WARN, "<admincmd> error creating file '%s': %s", buf, err);
	}

	fputs("\n#include arenas/(default)/arena.conf\n", f);
	fclose(f);

	chat->SendMessage(p, "Successfully created %s.", params);
}


local helptext_t botfeature_help =
"Targets: none\n"
"Args: [+/-{seeallposn}] [+/-{seeownposn}]\n"
"Enables or disables bot-specific features. {seeallposn} controls whether\n"
"the bot gets to see all position packets. {seeownposn} controls whether\n"
"you get your own mirror position packets.\n";

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
		else if (!strcmp(buf+1, "seeownposn"))
			p->flags.see_own_posn = on;
		else
			chat->SendMessage(p, "Unknown bot feature");
	}
}



EXPORT int MM_admincmd(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
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
		cmd->AddCommand("putmap", Cputmap, putmap_help);
		cmd->AddCommand("makearena", Cmakearena, makearena_help);
		cmd->AddCommand("botfeature", Cbotfeature, botfeature_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("admlogfile", Cadmlogfile);
		cmd->RemoveCommand("getfile", Cgetfile);
		cmd->RemoveCommand("putfile", Cputfile);
		cmd->RemoveCommand("putzip", Cputzip);
		cmd->RemoveCommand("putmap", Cputmap);
		cmd->RemoveCommand("makearena", Cmakearena);
		cmd->RemoveCommand("botfeature", Cbotfeature);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
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


