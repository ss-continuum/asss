
#ifndef __OOPUTILS_H
#define __OOPUTILS_H

#include <stddef.h>

size_t read_full(int fd, void *buf, size_t req);
void write_message(int fd, void *buf, int len);

#endif

