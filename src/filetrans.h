
/* dist: public */

#ifndef __FILETRANS_H
#define __FILETRANS_H


#define CB_UPLOADEDFILE ("uploadedfile")
typedef void (*UploadedFileFunc)(Player *p, const char *filename);


#define I_FILETRANS "filetrans-1"

typedef struct Ifiletrans
{
	INTERFACE_HEAD_DECL

	int (*SendFile)(Player *p, const char *path, const char *fname, int delafter);
	void (*RequestFile)(Player *p, const char *path, const char *fname);
} Ifiletrans;


#endif

