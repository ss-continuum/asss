
#ifndef __MAPNEWSDL_H
#define __MAPNEWSDL_H


typedef struct Imapnewsdl
{
	i32 (*GetMapChecksum)(int arena);
	char *(*GetMapFilename)(int arena);
	i32 (*GetNewsChecksum)();
} Imapnewsdl;

#endif

