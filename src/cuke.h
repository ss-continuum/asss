
#ifndef __CUKE_H
#define __CUKE_H

/* the maximum amount of data to pass around at once */
#define MAXCUKE 131072 /* 128k */

/* the cuke state to pass around */
typedef struct CukeState CukeState;

/* creating and freeing cukestates */
CukeState * new_cuke(void);
void free_cuke(CukeState *cuke);

/* sending and recving cukestates */
int raw_send_cuke(CukeState *cuke, int socket);
CukeState * raw_recv_cuke(int socket);

/* cuke types */
void set_cuke_type(CukeState *c, int type);
int get_cuke_type(CukeState *c);

/* write and read ints */
int w_uint(CukeState *cuke, unsigned int x);
unsigned int r_uint(CukeState *cuke);
#define w_int(cuke, x) w_uint((cuke), (unsigned int)(x))
#define r_int(cuke) ((int)(r_uint((cuke))))

/* read and write bytes */
int w_byte(CukeState *cuke, unsigned char b);
int r_byte(CukeState *cuke);
#define w_char(cuke, x) w_byte((cuke), (unsigned char)(x))
#define r_char(cuke) ((char)(r_byte((cuke))))

/* write and read sized opaque data */
int w_sized(CukeState *cuke, const void *str, int n);
int r_sized(CukeState *cuke, void *str, int n);

/* read and write strings */
const char * r_string(CukeState *cuke); /* remember to afree the result! */
int w_string(CukeState *cuke, const char *str);

#endif

