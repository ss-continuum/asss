
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <paths.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <io.h>
#endif


#include "asss.h"


#define CAP_UPLOADFILE "uploadfile"


struct upload_data
{
	FILE *fp;
	char *fname;
} uploads[MAXPLAYERS];

struct download_data
{
	FILE *fp;
	char *fname;
};


local Imodman *mm;
local Inet *net;
local Ilogman *lm;
local Icapman *capman;



local void p_inc_file(int pid, byte *data, int len)
{
	FILE *fp;
	char fname[32];

	if (capman && !capman->HasCapability(pid, CAP_UPLOADFILE))
		return;

	astrncpy(fname, "tmp/", sizeof(fname));
	strncat(fname, data+1, 16);

	fp = fopen(fname, "wb");
	if (!fp)
	{
		lm->LogP(L_WARN, "filetrans", pid, "Can't open '%s' for writing", fname);
		return;
	}

	fwrite(data+17, len-17, 1, fp);
	fclose(fp);

	DO_CBS(CB_UPLOADEDFILE, ALLARENAS, UploadedFileFunc, (pid, fname));
}


local void cleanup_ud(int pid, int success)
{
	struct upload_data *ud = uploads + pid;

	if (ud->fp)
	{
		fclose(ud->fp);
		ud->fp = NULL;
	}

	if (success)
	{
		DO_CBS(CB_UPLOADEDFILE, ALLARENAS, UploadedFileFunc, (pid, ud->fname));
	}
	else
	{
		if (unlink(ud->fname) == -1)
			lm->Log(L_WARN, "<filetrans> Can't unlink '%s': %s",
					ud->fname, strerror(errno));
	}

	afree(ud->fname);
}


local void sized_p_inc_file(int pid, byte *data, int len, int offset, int totallen)
{
	struct upload_data *ud = uploads + pid;

	if (offset == -1)
	{
		/* canceled */
		cleanup_ud(pid, 0);
	}
	else if (offset == 0 && ud->fp == NULL && len > 17)
	{
		if (!capman || capman->HasCapability(pid, CAP_UPLOADFILE))
		{
			char fname[PATH_MAX];
			astrncpy(fname, "tmp/", sizeof(fname));
			strncat(fname, data+1, 16);

			lm->LogP(L_INFO, "filetrans", pid, "Accepted '%.16s' for upload", data+1);

			ud->fname = astrdup(fname);
			ud->fp = fopen(fname, "wb");
			if (ud->fp)
				fwrite(data+17, len-17, 1, ud->fp);
			else
				lm->Log(L_WARN, "<filetrans> Can't open '%s' for writing", fname);
		}
		else
			lm->LogP(L_INFO, "filetrans", pid, "Denied file upload");
	}
	else if (offset > 0 && ud->fp)
	{
		if (offset < totallen)
			fwrite(data, len, 1, ud->fp);
		else
		{
			lm->LogP(L_INFO, "filetrans", pid, "Completed upload");
			cleanup_ud(pid, 1);
		}
	}
}


local void get_data(void *clos, int offset, byte *buf, int needed)
{
	struct download_data *dd = (struct download_data*)clos;

	if (needed == 0)
	{
		/* done */
		fclose(dd->fp);
		afree(dd->fname);
		afree(dd);
	}
	else if (offset == 0 && needed > 17)
	{
		*buf++ = S2C_INCOMINGFILE;
		strncpy(buf, dd->fname, 16);
		buf += 16;
		fread(buf, needed - 17, 1, dd->fp);
	}
	else if (offset > 0)
	{
		fread(buf, needed, 1, dd->fp);
	}
}


local int SendFile(int pid, const char *path, const char *fname)
{
	struct download_data *dd;
	struct stat st;
	FILE *fp;
	int ret;

	ret = stat(path, &st);
	if (ret == -1)
	{
		lm->Log(L_WARN, "<filetrans> Can't stat '%s': %s", path, strerror(errno));
		return MM_FAIL;
	}

	fp = fopen(path, "rb");
	if (!fp)
	{
		lm->Log(L_WARN, "<filetrans> Can't open '%s' for reading: %s", path, strerror(errno));
		return MM_FAIL;
	}

	dd = amalloc(sizeof(*dd));
	dd->fp = fp;
	dd->fname = astrdup(fname);

	net->SendSized(pid, dd, st.st_size + 17, get_data);
	lm->LogP(L_INFO, "filetrans", pid, "Sending '%s' (as '%s')", path, fname);
	return MM_OK;
}

local void RequestFile(int pid, const char *path, const char *fname)
{
	struct S2CRequestFile
	{
		u8 type;
		char path[256];
		char fname[16];
	} pkt;

	memset(&pkt, 0, sizeof(pkt));

	pkt.type = S2C_REQUESTFORFILE;
	astrncpy(pkt.path, path, 256);
	astrncpy(pkt.fname, fname, 16);

	net->SendToOne(pid, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	lm->LogP(L_INFO, "filetrans", pid, "Requesting file '%s' (as '%s')",
			path, fname);
}


/* interface */

local Ifiletrans _int =
{
	INTERFACE_HEAD_INIT(I_FILETRANS, "filetrans")
	SendFile, RequestFile
};


EXPORT int MM_filetrans(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		if (!net || !lm) return MM_FAIL;

		net->AddPacket(C2S_UPLOADFILE, p_inc_file);
		net->AddSizedPacket(C2S_UPLOADFILE, sized_p_inc_file);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		net->RemovePacket(C2S_UPLOADFILE, p_inc_file);
		net->RemoveSizedPacket(C2S_UPLOADFILE, sized_p_inc_file);

		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}

