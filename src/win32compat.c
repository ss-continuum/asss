
#include <stdio.h>
#include <string.h>

#include "win32compat.h"


/* taken from
 * http://www2.ics.hawaii.edu/~esb/2001fall.ics451/strcasestr.html */
const char * strcasestr(const char* haystack, const char* needle)
{
	int i;
	int nlength = strlen (needle);
	int hlength = strlen (haystack);

	if (nlength > hlength) return NULL;
	if (hlength <= 0) return NULL;
	if (nlength <= 0) return haystack;
	/* hlength and nlength > 0, nlength <= hlength */
	for (i = 0; i <= (hlength - nlength); i++)
		if (strncasecmp (haystack + i, needle, nlength) == 0)
			return haystack + i;
	/* substring not found */
	return NULL;
}


int mkstemp(char *template)
{
	if (mktemp(template) == NULL)
		return -1;
	else
		return open(template);
}


