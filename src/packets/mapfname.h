
#ifndef __PACKETS_MAPFNAME_H
#define __PACKETS_MAPFNAME_H

/* mapfname.h - map filename packet */


struct MapFilename
{
    u8 type;
    char filename[16];
    u32 checksum;
};

#endif

