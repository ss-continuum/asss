
#include <unistd.h>

#include "ooputils.h"



size_t read_full(int fd, void *_buf, size_t req)
{
	int left, ret;
	char *buf;

	left = req;
	buf = _buf;

	while (left > 0)
	{
		ret = read(fd, buf, left);
		if (ret == 0)
			return 0;
		if (ret > 0)
		{
			left -= ret;
			buf += ret;
		}
	}
	return req;
}


size_t write_full(int fd, void *_buf, size_t req)
{
	int left, ret;
	char *buf;

	left = req;
	buf = _buf;

	while (left > 0)
	{
		ret = write(fd, buf, left);
		if (ret == 0)
			return 0;
		if (ret > 0)
		{
			left -= ret;
			buf += ret;
		}
	}
	return req;
}


void write_message(int fd, void *buf, int len)
{
	write_full(fd, &len, sizeof(int));
	write_full(fd, buf, len);
}


