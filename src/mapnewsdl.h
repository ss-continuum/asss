
#ifndef __MAPNEWSDL_H
#define __MAPNEWSDL_H


typedef struct Imapnewsdl
{
	u32 (*GetMapChecksum)(int arena);
	char *(*GetMapFilename)(int arena);
	u32 (*GetNewsChecksum)();
} Imapnewsdl;

#endif

