
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


/* the bits of one of these represent those types above. only use the
 * following macros to access them. */
typedef unsigned short chat_mask_t;
#define IS_RESTRICTED(mask, type) ((mask) & (1<<(type)))
#define IS_ALLOWED(mask, type) (!IS_RESTRICTED(mask, type))
#define SET_RESTRICTED(mask, type) (mask) |= (1<<(type))
#define SET_ALLOWED(mask, type) (mask) &= ~(1<<(type))


#define I_CHAT "chat-2"

typedef struct Ichat
{
	INTERFACE_HEAD_DECL

	void (*SendMessage)(int pid, const char *format, ...);
	/* arpc: void(int, formatstr, etc) */
	void (*SendSetMessage)(int *set, const char *format, ...);
	/* arpc: void(intset, formatstr, etc) */
	void (*SendSoundMessage)(int pid, char sound, const char *format, ...);
	/* arpc: void(int, char, formatstr, etc) */
	void (*SendSetSoundMessage)(int *set, char sound, const char *format, ...);
	/* arpc: void(intset, char, formatstr, etc) */
	void (*SendAnyMessage)(int *set, char type, char sound, const char *format, ...);
	/* arpc: void(intset, char, char, formatstr, etc) */
	void (*SendArenaMessage)(int arena, const char *format, ...);
	/* arpc: void(int, formatstr, etc) */
	void (*SendArenaSoundMessage)(int arena, char sound, const char *format, ...);
	/* arpc: void(int, char, formatstr, etc) */

	/* in the above two, use arena == ALLARENAS for zone. */

	chat_mask_t (*GetArenaChatMask)(int arena);
	/* arpc: ushort(int) */
	void (*SetArenaChatMask)(int arena, chat_mask_t mask);
	/* arpc: void(int, ushort) */
	chat_mask_t (*GetPlayerChatMask)(int pid);
	/* arpc: ushort(int) */
	void (*SetPlayerChatMask)(int pid, chat_mask_t mask, int timeout);
	/* arpc: void(int, ushort, int) */
	/* (timeout is 0 to mean 'for a session' mask, or a number of seconds) */
} Ichat;


#endif

