
/* dist: public */

#ifndef __FILETRANS_H
#define __FILETRANS_H


#define I_FILETRANS "filetrans-1"

typedef struct Ifiletrans
{
	INTERFACE_HEAD_DECL

	int (*SendFile)(Player *p, const char *path, const char *fname, int delafter);
	/* uploaded will get called when the file is done being uploaded.
	 * filename will be the name of the uploaded file. if filename == NULL,
	 * there was an error and you should clean up any allocated memory
	 * in clos. */
	void (*RequestFile)(Player *p, const char *path,
			void (*uploaded)(const char *filename, void *clos), void *clos);
} Ifiletrans;


#endif

