
#define ___dummy \
: /*
set -e
gcc -g -DNOTHREAD -I../src ../src/app.c ../src/util.c app.c -o app -lpthread
./app /dev/stdin
exit
*/

#include <stdio.h>

#include "app.h"


static int finder(char *dest, int destlen, const char *arena, const char *name)
{
	astrncpy(dest, name, destlen);
}

static void err(const char *error)
{
	fputs(error, stderr);
	fputc('\n', stderr);
}

int main(int argc, char *argv[])
{
	int i;
	APPContext *ctx;
	char buf[1024];

	ctx = InitContext(finder, err, "");
	for (i = 1; i < argc; i++)
		AddFile(ctx, argv[i]);

	while (GetLine(ctx, buf, 1024))
		puts(buf);

	FreeContext(ctx);
}


