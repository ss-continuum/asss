
/* dist: public */

#ifndef __BANNERS_H
#define __BANNERS_H

#include "packets/banners.h"

#define CB_SET_BANNER ("setbanner")
typedef void (*SetBannerFunc)(Player *p, Banner *banner);
/* called when the player sets a new banner */


#define I_BANNERS "banners-1"

typedef struct Ibanners
{
	INTERFACE_HEAD_DECL
	void (*SetBanner)(Player *p, Banner *banner);
	/* sets banner. NULL to remove. */
} Ibanners;

#endif

