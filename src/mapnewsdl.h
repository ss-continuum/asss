
#ifndef __MAPNEWSDL_H
#define __MAPNEWSDL_H

#define I_MAPNEWSDL "mapnewsdl-1"

typedef struct Imapnewsdl
{
	INTERFACE_HEAD_DECL

	void (*SendMapFilename)(int pid);
	u32 (*GetNewsChecksum)();
} Imapnewsdl;

#endif

