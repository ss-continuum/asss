
#ifndef __MAPNEWSDL_H
#define __MAPNEWSDL_H

typedef struct Imapnewsdl
{
	void (*SendMapFilename)(int pid);
	u32 (*GetNewsChecksum)();
} Imapnewsdl;

#endif

