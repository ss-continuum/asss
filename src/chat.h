
/* dist: public */

#ifndef __CHAT_H
#define __CHAT_H

#include "packets/chat.h"


/* types of chat messages */
#define MSG_ARENA        0
#define MSG_PUBMACRO     1
#define MSG_PUB          2
#define MSG_FREQ         3
#define MSG_TEAM         3
#define MSG_NMEFREQ      4
#define MSG_PRIV         5
#define MSG_REMOTEPRIV   7
#define MSG_SYSOPWARNING 8
#define MSG_CHAT         9
/* the following are for internal use only. they never appear in packets
 * sent over the network. */
#define MSG_MODCHAT      10
#define MSG_COMMAND      11
#define MSG_BCOMMAND     12


/* called for most types of chat msg. type is one of the above codes.
 * target is only valid for MSG_PRIV and MSG_REMOTEPRIV; freq is
 * only valid for MSG_FREQ and MSG_NMEFREQ. */
#define CB_CHATMSG "chatmsg"
typedef void (*ChatMsgFunc)(Player *p, int type, int sound, Player *target,
		int freq, const char *text);
/* pycb: player, int, int, player, int, string */


/* the bits of one of these represent those types above. only use the
 * following macros to access them. */
typedef unsigned short chat_mask_t;
#define IS_RESTRICTED(mask, type) ((mask) & (1<<(type)))
#define IS_ALLOWED(mask, type) (!IS_RESTRICTED(mask, type))
#define SET_RESTRICTED(mask, type) (mask) |= (1<<(type))
#define SET_ALLOWED(mask, type) (mask) &= ~(1<<(type))


#define I_CHAT "chat-5"

typedef struct Ichat
{
	INTERFACE_HEAD_DECL

	/* pyint: use */
	/* NOTE: that things involving player sets (lists) aren't supported yet. */

	void (*SendMessage)(Player *p, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: player, formatted -> void */
	void (*SendSetMessage)(LinkedList *set, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	void (*SendSoundMessage)(Player *p, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	/* pyint: player, int, formatted -> void */
	void (*SendSetSoundMessage)(LinkedList *set, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	void (*SendAnyMessage)(LinkedList *set, char type, char sound, Player *from, const char *format, ...)
		ATTR_FORMAT(printf, 5, 6);
	void (*SendArenaMessage)(Arena *arena, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: arena, formatted -> void */
	void (*SendArenaSoundMessage)(Arena *arena, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	/* pyint: arena, int, formatted -> void */
	/* in the above two, use arena == ALLARENAS for zone. */
	void (*SendModMessage)(const char *format, ...)
		ATTR_FORMAT(printf, 1, 2);
	/* pyint: formatted -> void */

	chat_mask_t (*GetArenaChatMask)(Arena *arena);
	/* pyint: arena -> int */
	void (*SetArenaChatMask)(Arena *arena, chat_mask_t mask);
	/* pyint: arena, int -> void */
	chat_mask_t (*GetPlayerChatMask)(Player *p);
	/* pyint: player -> int */
	void (*SetPlayerChatMask)(Player *p, chat_mask_t mask, int timeout);
	/* (timeout is 0 to mean 'for a session' mask, or a number of seconds) */
	/* pyint: player, int, int -> void */
} Ichat;


#endif

