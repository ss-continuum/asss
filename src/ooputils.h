
#ifndef __OOPUTILS_H
#define __OOPUTILS_H

#include <stddef.h>

size_t read_full(int fd, void *_buf, size_t req);
size_t write_full(int fd, void *_buf, size_t req);
void write_message(int fd, void *buf, int len);

#endif

