#!/usr/bin/env python

# gensparse.py
# generates C code for efficient sparse array manipulation


# PARAMETERS -----------------------------------------------------------

# the type of the data to be represented
sourcetype = 'byte'

# the type of the sparse array to be created
targettype = 'sparse_arr'

# the default (common) value
default = '0'

# the function to use for allocating memory
malloc = 'amalloc'

# the function to use to free memory
free = 'afree'

# the numbers of bits at each level of indirection
bits = [3, 3, 4]


# CODE (don't touch below here) ----------------------------------------

import sys

def maxcoord(idx):
	return 2**bits[idx] - 1


def gen_name(idx):
	if idx == 0:
		return sourcetype
	else:
		return 'sparse_chunk_%d_' % idx


def emit_types(o):
	"typedefs"
	def gen_array_typedef(dat, ptr, name, size):
		if ptr:
			return 'typedef %s *%s[%d][%d];\n' % (dat, name, size, size)
		else:
			return 'typedef %s %s[%d][%d];\n' % (dat, name, size, size)

	for idx in range(len(bits)):
		o.write(
			gen_array_typedef(gen_name(idx),
			                  idx > 0,
			                  gen_name(idx+1),
			                  2**bits[idx]))
	o.write('typedef %s *%s;\n' % (gen_name(len(bits)), targettype))


def emit_init(o):
	"allocate memory/initialize"
	o.write("""
%(target)s init_sparse(void)
{
	int x, y;
	%(target)s c;

	c = %(malloc)s(sizeof(*%(target)s));
	if (c == NULL)
		return NULL;

	for (x = 0; x < %(max)d; x++)
		for (y = 0; y < %(max)d; y++)
			(*c)[x][y] = NULL;
	return c;
}
""" %
	{
		'target': targettype,
		'malloc': malloc,
		'max': maxcoord(-1)
	})


def emit_delete(o):
	"release memory"
	def gen_loop(body, idx):
		dict = \
		{
			'max': maxcoord(idx),
			'body': body,
			'type': gen_name(idx),
			'c0': 'c_%d' % idx,
			'c1': 'c_%d' % (idx + 1),
			'free': free
		}

		doloop = """\
	int x, y;
	for (x = 0; x < %(max)d; x++)
		for (y = 0; y < %(max)d; y++)
		{
			%(type)s *%(c0)s = (*%(c1)s)[x][y];
			if (%(c0)s)
			{
				%(body)s
			}
		}""" % dict

		dofree = "\n\t%(free)s(%(c1)s);" % dict

		if idx == 0:
			return dofree
		else:
			return doloop + dofree

	o.write("""
void delete_sparse(%(target)s c)
{
	%(type)s *%(c1)s = c;
	%(body)s
}
""" % \
	{
		'target': targettype,
		'type': gen_name(len(bits)),
		'c1': 'c_%d' % len(bits),
		'body': reduce(gen_loop, range(len(bits)), '')
	})


def emit_all(o):
	for f in [emit_types, emit_init, emit_delete, emit_lookup, emit_insert]:
		o.write('\n/* section: %s */\n' % f.__doc__)
		f(o)
	o.write('\n/* done */\n')

#emit_all(sys.stdout)
emit_types(sys.stdout)
emit_init(sys.stdout)
emit_delete(sys.stdout)


"""

the C code that I wrote is here:


/* bigchunksize is the log of the number of smaller chunks in the whole
 * map */
#define BIGSIZE 4
/* smallchunksize is the number of data_t's in each small chunk */
#define SMALLSIZE 6

#define MAXCOORD(x) ((1<<(x))-1)

#define BIGBITS(x) \
	(((x) >> SMALLBITS) & MAXCOORD(BIGSIZE))
#define SMALLBITS(x) \
	((x) & MAXCOORD(SMALLSIZE))


typedef byte data_t;

typedef data_t smallchunk[1<<SMALLSIZE][1<<SMALLSIZE];

typedef chunk1 *bigchunk[1<<BIGSIZE][1<<BIGSIZE];


inline data_t lookup_chunk(bigchunk *d, int x, int y, data_t def)
{

	smallchunk *c = d[BIGBITS(x)][BIGBITS(y)];
	if (c)
		return c[SMALLBITS(x)][SMALLBITS(y)];
	else
		return def;
};

bigchunk *init_chunk()
{
	int x, y;
	bigchunk *c = malloc(sizeof(bigchunk));
	for (x = 0; x < MAXCOORD(BIGSIZE); x++)
		for (y = 0; y < MAXCOORD(BIGSIZE); y++)
			c[x][y] = NULL;
	return c;
}

void delete_chunk(bigchunk *d)
{
	int x, y;
	for (x = 0; x < MAXCOORD(BIGSIZE); x++)
		for (y = 0; y < MAXCOORD(BIGSIZE); y++)
			if (d[x][y])
				free(d[x][y]);
	free(d);
}

inline void insert_chunk(bigchunk *d, int x, int y, data_t def, data_t dat)
{
	smallchunk *c = d[BIGBITS(x)][BIGBITS(y)];
	if (!c)
	{
		int i, j;
		c = malloc(sizeof(smallchunk));
		for (i = 0; i < MAXCOORD(SMALLSIZE); i++)
			for (j = 0; j < MAXCOORD(SMALLSIZE); j++)
				c[i][j] = def;
		d[BIGBITS(x)][BIGBITS(y)] = c;
	}
	c[SMALLBITS(x)][SMALLBITS(y)] = dat;
}


"""


