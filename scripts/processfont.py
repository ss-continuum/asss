#!/usr/bin/env python


"""
process a font file into a big C array for inclusion in source.

the font file will have 95n lines, where n is the height of each
character.

each character will be n rows of '[.#]+'. dots signify the absence of a
brick, pound signs, the presense.

the C array that we will be generating looks like this:

/* declarations */

struct bl_brick
{
	unsigned char x1, y1, x2, y2;
};

struct bl_letter
{
	int width, bricknum;
	struct bl_brick *bricks; /* can be null if bricknum == 0 */
};


/* letters */

/* ... */

/* - */
struct bl_brick bl_letter_45[1] =
{
	{ 0, 3, 4, 3 }
}

/* ... */


/* the big array */

struct bl_letter letterdata[95] =
{
	/* ... */
	{ 5, 1, bl_letter_45 },
	/* ... */
}

"""


