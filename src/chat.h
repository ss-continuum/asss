
/* dist: public */

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

/* the following are for internal use only. they never appear in packets
 * sent over the network. */
#define MSG_MODCHAT 10
#define MSG_COMMAND 11
#define MSG_BCOMMAND 12


/* called for most types of chat msg. type is one of the above codes. if
 * type == MSG_PRIV, target will be a pid. if type == MSG_FREQ or
 * MSG_NMEFREQ, target will be a freq. otherwise unused. */
#define CB_CHATMSG "chatmsg"
typedef void (*ChatMsgFunc)(int pid, int type, int target, const char *text);


/* the bits of one of these represent those types above. only use the
 * following macros to access them. */
typedef unsigned short chat_mask_t;
#define IS_RESTRICTED(mask, type) ((mask) & (1<<(type)))
#define IS_ALLOWED(mask, type) (!IS_RESTRICTED(mask, type))
#define SET_RESTRICTED(mask, type) (mask) |= (1<<(type))
#define SET_ALLOWED(mask, type) (mask) &= ~(1<<(type))


#define I_CHAT "chat-3"

typedef struct Ichat
{
	INTERFACE_HEAD_DECL

	void (*SendMessage)(int pid, const char *format, ...);
	void (*SendSetMessage)(int *set, const char *format, ...);
	void (*SendSoundMessage)(int pid, char sound, const char *format, ...);
	void (*SendSetSoundMessage)(int *set, char sound, const char *format, ...);
	void (*SendAnyMessage)(int *set, char type, char sound, const char *format, ...);
	void (*SendArenaMessage)(Arena *arena, const char *format, ...);
	void (*SendArenaSoundMessage)(Arena *arena, char sound, const char *format, ...);
	/* in the above two, use arena == ALLARENAS for zone. */
	void (*SendModMessage)(const char *format, ...);

	chat_mask_t (*GetArenaChatMask)(Arena *arena);
	void (*SetArenaChatMask)(Arena *arena, chat_mask_t mask);
	chat_mask_t (*GetPlayerChatMask)(int pid);
	void (*SetPlayerChatMask)(int pid, chat_mask_t mask, int timeout);
	/* (timeout is 0 to mean 'for a session' mask, or a number of seconds) */
} Ichat;


#endif

