
/* dist: public */

#include <stdlib.h>
#include <assert.h>

#include "util.h"
#include "region.h"

/* functions */

coordlist_t init_coordlist(void)
{
	coordlist_t l;
	l.count = 0;
	l.allocated = 10;
	l.data = amalloc(l.allocated * sizeof(coord_t));
	return l;
}

void add_coord(coordlist_t *l, int x, int y)
{
	coord_t c;
	c.x = x;
	c.y = y;
	if (l->count == l->allocated)
	{
		l->allocated *= 2;
		l->data = realloc(l->data, l->allocated * sizeof(coord_t));
		assert(l->data);
	}
	l->data[l->count++] = c;
}

void delete_coordlist(coordlist_t l)
{
	afree(l.data);
}

rectlist_t init_rectlist(void)
{
	rectlist_t l;
	l.count = 0;
	l.allocated = 5;
	l.data = amalloc(l.allocated * sizeof(rect_t));
	return l;
}

void add_rect(rectlist_t *l, rect_t rect)
{
	if (l->count >= l->allocated)
	{
		l->allocated *= 2;
		l->data = realloc(l->data, l->allocated * sizeof(rect_t));
		assert(l->data);
	}
	l->data[l->count++] = rect;
}

void delete_rectlist(rectlist_t l)
{
	afree(l.data);
}


/* region file encoding */

#define ENCODINGLEN 8

char val_to_char(int val)
{
	if (val >= 0 && val <= 25)
		return val + 'a';
	else if (val >= 26 && val <= 31)
		return val + '1' - 26;
	else return -1;
}

int char_to_val(char c)
{
	if (c >= 'a' && c <= 'z')
		return c - 'a';
	else if (c >= '1' && c <= '6')
		return c - '1' + 26;
	else return -1;
}

/* stores an encoding of the rectangle r to the string s, which must
 * have at least ENCODINGLEN+1 bytes free */
void encode_rectangle(char *s, rect_t r)
{
#define DO(v) \
	*s++ = val_to_char( ((v) >> 5) & 31 ); \
	*s++ = val_to_char( ((v) >> 0) & 31 );
	DO(r.x) DO(r.y) DO(r.w) DO(r.h)
#undef DO
	*s = 0;
}


rect_t decode_rectangle(char *s)
{
	rect_t r;
#define DO(v) \
	(v) = char_to_val(s[0]) << 5 | char_to_val(s[1]); s += 2;
	DO(r.x) DO(r.y) DO(r.w) DO(r.h)
#undef DO
	return r;
}

