
/* dist: public */

#ifndef __PRNG_H
#define __PRNG_H

/* Iprng
 * this module will service all requests for pseudo-random numbers.
 */

#define I_PRNG "prng-1"

typedef struct Iprng
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* "good" functions */

	/* fills a buffer with really secure random bits, returns true on
	 * success. */
	int (*GoodFillBuffer)(void *buf, int size);

	/* "decent" functions */

	/* fills a buffer with random bits, returns true on success. */
	int (*FillBuffer)(void *buf, int size);

	/* returns a random number between two inclusive bounds. */
	int (*Number)(int start, int end);
	/* pyint: int, int -> int */

	/* returns a random 32-bit number. */
	u32 (*Get32)(void);

	/* returns a number from 0 to RAND_MAX. */
	int (*Rand)(void);
	/* pyint: void -> int */

	/* returns a double uniformly distributed from 0 to 1. */
	double (*Uniform)(void);
	/* pyint: void -> double */
} Iprng;

#endif
