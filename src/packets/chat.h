
#ifndef __PACKETS_CHAT_H
#define __PACKETS_CHAT_H

/* chat.h - chat packet */


struct ChatPacket
{
	i8 pktype;
	i8 type, sound;
	i16 pid;
	char text[0];
};

#endif

