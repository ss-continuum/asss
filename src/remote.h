
#ifndef __REMOTE_H
#define __REMOTE_H

#include "cuke.h"

typedef struct ConnectionData ConnectionData;

ConnectionData * new_con_data(int socket);
void deinit_con_data(ConnectionData *con);
void send_cuke(ConnectionData *con, CukeState *cuke);
CukeState * wait_for_resp(ConnectionData *con, int type);

#define send_cuke_default(cuke) send_cuke(NULL, (cuke))
#define wait_for_resp_default(type) wait_for_resp(NULL, (type))

#endif

