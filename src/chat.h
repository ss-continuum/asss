
#ifndef __CHAT_H
#define __CHAT_H

#include "packets/chat.h"


/* types of chat messages */
#define MSG_ARENA 0
#define MSG_PUBMACRO 1
#define MSG_PUB 2
#define MSG_FREQ 3
#define MSG_TEAM 3
#define MSG_NMEFREQ 4
#define MSG_PRIV 5
#define MSG_INTERARENAPRIV 7
#define MSG_SYSOPWARNING 8
#define MSG_CHAT 9


typedef struct Ichat
{
	void (*SendMessage)(int pid, char *format, ...);
	void (*SendSetMessage)(int *set, char *format, ...);
	void (*SendSoundMessage)(int pid, char sound, char *format, ...);
	void (*SendSetSoundMessage)(int *set, char sound, char *format, ...);
} Ichat;


#endif

