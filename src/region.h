
/* dist: public */

#ifndef __REGION_H
#define __REGION_H

/* structs */

typedef struct coord_t
{
	int x,y;
} coord_t;

typedef struct coordlist_t
{
	coord_t *data;
	int count, allocated;
} coordlist_t;

typedef struct rect_t
{
	int x, y, w, h;
} rect_t;

typedef struct rectlist_t
{
	rect_t *data;
	int count, allocated;
} rectlist_t;


/* functions */

coordlist_t init_coordlist(void);
void add_coord(coordlist_t *l, int x, int y);
void delete_coordlist(coordlist_t l);
rectlist_t init_rectlist(void);
void add_rect(rectlist_t *l, rect_t rect);
void delete_rectlist(rectlist_t l);
char val_to_char(int val);
int char_to_val(char c);
void encode_rectangle(char *s, rect_t r);
rect_t decode_rectangle(char *s);

#define HEADER "asss region file version 1"

#endif

