
#ifndef __ENCRYPT_H
#define __ENCRYPT_H

/*
 * Iencrypt - encryption methods
 *
 * unless you plan on replacing the encryption algorithm in the client
 * and server, don't bother with this.
 *
 */

typedef struct Iencrypt
{
	int  (*Respond)	(int);
	void (*Init)	(int, int);
	void (*Encrypt)	(int, char *, int);
	void (*Decrypt)	(int, char *, int);
} Iencrypt;

#endif

