
#ifndef WIN32
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#else
#include <io.h>
#include <winsock.h>
#endif


#include "util.h"
#include "cuke.h"


struct CukeState
{
	int type, seqnum; /* the packet type and sequence number */
	unsigned char *str, *cur, *end; /* for writing to non-files */
};

/* from ooputils */
static inline size_t read_full(int fd, void *_buf, size_t req)
{
	int left, ret;
	char *buf;

	left = req;
	buf = _buf;

	while (left > 0)
	{
		ret = read(fd, buf, left);
		if (ret == 0 || ret == -1)
			return ret;
		if (ret > 0)
		{
			left -= ret;
			buf += ret;
		}
	}
	return req;
}


static inline size_t write_full(int fd, void *_buf, size_t req)
{
	int left, ret;
	char *buf;

	left = req;
	buf = _buf;

	while (left > 0)
	{
		ret = write(fd, buf, left);
		if (ret == 0 || ret == -1)
			return ret;
		if (ret > 0)
		{
			left -= ret;
			buf += ret;
		}
	}
	return req;
}


/* stuff to read and write bytes and slightly larger units */

/* non-zero on failure */
static int expand_cuke_string(CukeState *cuke)
{
	int len;
	unsigned char *newstr;

	if (!cuke->str || !cuke->cur)
		return 1;

	len = (cuke->end - cuke->str) * 2;
	newstr = realloc(cuke->str, len);
	if (!newstr)
		return 1;
	cuke->cur = newstr + (cuke->cur - cuke->str);
	cuke->str = newstr;
	cuke->end = cuke->str + len;
	return 0;
}


inline int w_byte(CukeState *cuke, unsigned char b)
{
	if (cuke->cur == cuke->end)
		if (expand_cuke_string(cuke))
			return 1;
	*(cuke->cur++) = b;
	return 0;
}

inline int r_byte(CukeState *cuke)
{
	if (cuke->cur == cuke->end)
		return -1;
	return *(cuke->cur++);
}

int w_uint(CukeState *cuke, unsigned int x)
{
	unsigned long towrite = htonl(x);
	return w_sized(cuke, &towrite, sizeof(towrite));
}

unsigned int r_uint(CukeState *cuke)
{
	unsigned long toread;
	r_sized(cuke, &toread, sizeof(toread));
	return ntohl(toread);
}

int w_sized(CukeState *cuke, const void *str, int n)
{
	if (cuke->end - cuke->cur >= n)
	{
		memcpy(cuke->cur, str, n);
		cuke->cur += n;
		return 0;
	}
	else /* if it can't fit, write byte by byte so it extends */
	{
		const unsigned char *s = str;
		while (n--)
			if (w_byte(cuke, *s++))
				return 1;
		return 0;
	}
}

int r_sized(CukeState *cuke, void *str, int n)
{
	if (cuke->end - cuke->cur >= n)
	{
		memcpy(str, cuke->cur, n);
		cuke->cur += n;
		return 0;
	}
	else
		return 1;
}

int w_string(CukeState *cuke, const char *str)
{
	if (!str)
		return w_int(cuke, -1);
	else
	{
		int size = strlen(str);
		w_int(cuke, htonl(size));
		return w_sized(cuke, str, size);
	}
}

const char * r_string(CukeState *cuke)
{
	int size = r_int(cuke);
	if (size == -1)
		return NULL;
	else
	{
		char *str = amalloc(size+1);
		r_sized(cuke, str, size);
		str[size] = 0;
		return str;
	}
}


/* creating and freeing cukestates */
CukeState * new_cuke(void)
{
	const int startsize = 128;
	CukeState *cuke = amalloc(sizeof(*cuke));
	cuke->type = 0;
	cuke->seqnum = 0;
	cuke->str = amalloc(startsize);
	cuke->cur = cuke->str;
	cuke->end = cuke->str + startsize;
	return cuke;
}

void free_cuke(CukeState *cuke)
{
	afree(cuke->str);
	afree(cuke);
}


/* sending and recving cukestates
 *
 * the protocol is dead simple: 4 bytes of len, 4 bytes of type, 4 bytes
 * of seqnum, then len bytes of cuke data. */

int raw_send_cuke(CukeState *cuke, int socket)
{
	int ret;
	int w_len = htonl(cuke->cur - cuke->str);
	int w_type = htonl(cuke->type);
	int w_sn = htonl(cuke->seqnum);

	ret = write_full(socket, &w_len, sizeof(w_len));
	if (ret == -1 || ret == 0) return 1;
	ret = write_full(socket, &w_type, sizeof(w_type));
	if (ret == -1 || ret == 0) return 1;
	ret = write_full(socket, &w_sn, sizeof(w_sn));
	if (ret == -1 || ret == 0) return 1;
	ret = write_full(socket, cuke->str, cuke->cur - cuke->str);
	if (ret == -1 || ret == 0) return 1;
	return 0;
}


CukeState * raw_recv_cuke(int socket)
{
	CukeState *cuke;
	int len, type, sn, ret;

	ret = read_full(socket, &len, sizeof(len));
	if (ret == -1 || ret == 0) return NULL;
	if (len < 1 || len > MAXCUKE) return NULL;
	ret = read_full(socket, &type, sizeof(type));
	if (ret == -1 || ret == 0) return NULL;
	ret = read_full(socket, &sn, sizeof(sn));
	if (ret == -1 || ret == 0) return NULL;

	len = ntohl(len);
	type = ntohl(type);
	sn = ntohl(sn);

	cuke = amalloc(sizeof(*cuke));
	cuke->type = type;
	cuke->seqnum = sn;
	cuke->str = amalloc(len);
	cuke->cur = cuke->str;
	cuke->end = cuke->str + len;

	ret = read_full(socket, cuke->str, len);
	if (ret == -1 || ret == 0)
	{
		free_cuke(cuke);
		return NULL;
	}
	return cuke;
}


