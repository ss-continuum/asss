
/* dist: public */

#ifndef __CHATNET_H
#define __CHATNET_H


typedef void (*MessageFunc)(int pid, const char *line);
/* a func of this type is responsible for one message type. the line
 * passed in will be the line sent by the client, minus the message type
 * field. */


struct chat_client_stats
{
	/* ip info */
	char ipaddr[16];
	unsigned short port;
};


#define I_CHATNET "chatnet-1"

typedef struct Ichatnet
{
	INTERFACE_HEAD_DECL

	void (*AddHandler)(const char *type, MessageFunc func);
	void (*RemoveHandler)(const char *type, MessageFunc func);

	void (*SendToOne)(int pid, const char *line, ...);
	void (*SendToArena)(int arena, int except, const char *line, ...);
	void (*SendToSet)(int *set, const char *line, ...);

	void (*DropClient)(int pid);

	void (*GetClientStats)(int pid, struct chat_client_stats *stats);

} Ichatnet;

#endif

