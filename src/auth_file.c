
/* this module authenticates players based on hashed passwords from a
 * file. the file should look rougly like:

[General]
AllowUnknown = yes

[Users]
Grelminar = 561fb382be64fdfd6c4bc4450577b966
other.player = 37b51d194a7513e45b56f6524f2d51f2
bad-person = locked
someone = any

 * the file should be in conf/passwd.conf.
 *
 * for players listed in users: if the key has the value "locked", they
 * will be denied entry. if it's set to "any", they will be allowed in
 * with any password. otherwise, it's a md5 hash of their player name
 * and password, set by the ?passwd command.
 */

#include <string.h>

#include "asss.h"

#include "md5.h"

local Iplayerdata *pd;
local Iconfig *cfg;
local Icmdman *cmd;
local ConfigHandle pwdfile;


local void hash_password(const char *name, const char *pwd, char out[33])
{
	static const char table[16] = "0123456789abcdef";
	struct MD5Context ctx;
	unsigned char hash[16];
	char msg[56];
	int i;

	memset(msg, 0, 56);
	astrncpy(msg, name, 24);
	ToLowerStr(msg);
	astrncpy(msg + 24, pwd, 32);

	MD5Init(&ctx);
	MD5Update(&ctx, msg, 56);
	MD5Final(hash, &ctx);

	for (i = 0; i < 16; i++)
	{
		out[i*2+0] = table[(hash[i] & 0xf0) >> 4];
		out[i*2+1] = table[(hash[i] & 0x0f) >> 0];
	}
	out[32] = 0;
}


local void authenticate(int pid, struct LoginPacket *lp, int lplen,
		void (*done)(int pid, AuthData *data))
{
	AuthData ad;
	const char *line;
	char name[24];

	/* copy to local storage in case it's not null terminated */
	astrncpy(name, lp->name, sizeof(name));

	/* setup basic authdata */
	memset(&ad, 0, sizeof(ad));
	ad.code = AUTH_OK;
	astrncpy(ad.name, name, sizeof(ad.name));
	astrncpy(ad.sendname, name, sizeof(ad.sendname));

	/* check if this user has an entry */
	line = cfg->GetStr(pwdfile, "users", name);

	if (line)
	{
		if (!strcmp(line, "lock"))
			ad.code = AUTH_NOPERMISSION;
		else if (!strcmp(line, "any"))
			ad.code = AUTH_OK;
		else
		{
			char hex[33];

			hash_password(name, lp->password, hex);

			if (strcmp(hex, line))
				ad.code = AUTH_BADPASSWORD;
			else
				ad.code = AUTH_OK;
		}
	}
	else
	{
		/* no match found */

		/* cfghelp: General:AllowUnknown, passwd.conf, bool, def: 1, \
		 * mod: auth_file
		 * Determines whether to allow players not listed in the
		 * password file. */
		int allow = cfg->GetInt(pwdfile, "General", "AllowUnknown", 1);

		if (allow)
			ad.code = AUTH_OK;
		else
			ad.code = AUTH_NOPERMISSION;
	}

	done(pid, &ad);
}


local helptext_t passwd_help =
"Targets: none\n"
"Args: <new password>\n"
"Changes your local server password. Note that this command only changes\n"
"the password used by the auth_file authentication mechanism. The billing\n"
"server is not involved at all.\n";

local void Cpasswd(const char *params, int pid, const Target *target)
{
	char hex[33];

	if (!*params)
		return;

	hash_password(pd->players[pid].name, params, hex);

	cfg->SetStr(pwdfile, "users", pd->players[pid].name, hex, NULL);
}



local Iauth myauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "auth-file", 5)
	authenticate
};


EXPORT int MM_auth_file(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!cfg || !cmd || !pd) return MM_FAIL;

		pwdfile = cfg->OpenConfigFile(NULL, "passwd.conf", NULL, NULL);

		if (!pwdfile) return MM_FAIL;

		cmd->AddCommand("passwd", Cpasswd, passwd_help);

		mm->RegInterface(&myauth, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("passwd", Cpasswd);
		cfg->CloseConfigFile(pwdfile);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}

