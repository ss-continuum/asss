
/*
 * threads:
 *
 * one to recv cukes.
 * processing incoming cukes can be done from a mainloop method.
 * sending outgoing cukes can be done on the spot, with a mutex around
 * the socket.
 *
 * that way, each new connection only needs one new thread.
 *
 * OR: use select in the mainloop method and avoid the recv thread too?
 *
 */


#include "asss.h"
#include "remote.h"


typedef struct ConnectionData
{
	int socket;
	/* this mutex only protects writes to the socket, because they can
	 * occur at any time. reads only ever occur from one place in one
	 * thread, so we don't have to protect them. */
	pthread_mutex_t send_mtx;

	/* this holds the thread id of this connection's thread */
	pthread_t recvthread;

	/* this queue is used for incoming requests */
	LinkedList req_q;
	pthread_mutex_t req_mtx;
	pthread_cond_t req_cond;

	/* this list is for incoming responses */
	LinkedList resp_q;
	pthread_mutex_t resp_mtx;
	pthread_cond_t resp_cond;
} ConnectionData;


/* prototypes */
local void * recv_thread(void *dummy);
local void loop_processcukes(void);

/* the list of active connections */
local LinkedList conns;
local pthread_mutex_t conn_mtx = PTHREAD_MUTEX_INITIALIZER;

/* the default connection (only used on the remote side!) */
local ConnectionData *default_conn;


/* making new condata structs */

ConnectionData * new_con_data(int socket)
{
	/* alloc */
	ConnectionData *con = amalloc(sizeof(*con));

	/* set up */
	con->socket = socket;
	pthread_mutex_init(&con->send_mtx, NULL);
	LLInit(&con->req_q);
	pthread_mutex_init(&con->req_mtx, NULL);
	pthread_cond_init(&con->req_cond, NULL);
	LLInit(&con->resp_q);
	pthread_mutex_init(&con->resp_mtx, NULL);
	pthread_cond_init(&con->resp_cond, NULL);

	/* spawn it's recvthread */
	pthread_create(&con->recvthread, NULL, recv_thread, con);

	/* add it to list */
	pthread_mutex_lock(&conn_mtx);
	LLAdd(&conns, con);
	pthread_mutex_unlock(&conn_mtx);

	return con;
}

void deinit_con_data(ConnectionData *con)
{
	/* remove it from list */
	pthread_mutex_lock(&conn_mtx);
	LLRemove(&conns, con);
	pthread_mutex_unlock(&conn_mtx);

	/* kill thread */
	/* FIXME: make thread quit here. close socket? */
	pthread_join(con->recvthread, NULL);

	/* deinit */
	pthread_mutex_destroy(&con->send_mtx);
	LLEmpty(&con->req_q);
	pthread_mutex_destroy(&con->req_mtx);
	pthread_cond_destroy(&con->req_cond);
	LLEmpty(&con->resp_q);
	pthread_mutex_destroy(&con->resp_mtx);
	pthread_cond_destroy(&con->resp_cond);

	/* free it */
	afree(con);
}


void * recv_thread(void *dummy)
{
	ConnectionData *con = dummy;
	CukeState *cuke;
	int type;

	for (;;)
	{
		if (con->socket == 0) return NULL;
		cuke = raw_recv_cuke(con->socket);
		if (!cuke || con->socket == 0) return NULL;
		type = get_cuke_type(cuke);

		if (type < 0)
		{
			pthread_mutex_lock(&con->resp_mtx);
			LLAdd(&con->resp_q, cuke);
			/* let all threads waiting wake up and check if it's their
			 * response. */
			pthread_cond_broadcast(&con->resp_cond);
			pthread_mutex_unlock(&con->resp_mtx);
		}
		else if (type > 0)
		{
			pthread_mutex_lock(&con->req_mtx);
			LLAdd(&con->req_q, cuke);
			pthread_cond_broadcast(&con->req_cond);
			pthread_mutex_unlock(&con->req_mtx);
		}
		else
			free_cuke(cuke);
	}
}


local void process_cuke(CukeState *cuke)
{
}


void loop_processcukes()
{
	Link *l;
	CukeState *cuke;

	pthread_mutex_lock(&conn_mtx);
	for (l = LLGetHead(&conns); l; l = l->next)
	{
		ConnectionData *con = l->data;
		pthread_mutex_unlock(&conn_mtx);

		/* process incoming requests */
		pthread_mutex_lock(&con->req_mtx);
		cuke = LLRemoveFirst(&con->req_q);
		pthread_mutex_unlock(&con->req_mtx);
		if (cuke)
		{
			process_cuke(cuke);
			free_cuke(cuke);
		}

		pthread_mutex_lock(&conn_mtx);
	}
	pthread_mutex_unlock(&conn_mtx);
}


void send_cuke(ConnectionData *con, CukeState *cuke)
{
	if (!con) con = default_conn;
	if (!con) return;
	pthread_mutex_lock(&con->send_mtx);
	raw_send_cuke(cuke, con->socket);
	pthread_mutex_unlock(&con->send_mtx);
}


CukeState * wait_for_resp(ConnectionData *con, int type)
{
	if (!con) con = default_conn;
	if (!con) return;
	pthread_mutex_lock(&con->resp_mtx);
	for (;;)
	{
		/* check if we have one waiting */
		Link *l;
		for (l = LLGetHead(&con->resp_q); l; l = l->next)
		{
			CukeState *cuke = l->data;
			if (get_cuke_type(cuke) == type)
			{
				LLRemove(&con->resp_q, cuke);
				pthread_mutex_unlock(&con->resp_mtx);
				return cuke;
			}
		}

		/* if not, wait for more */
		pthread_cond_wait(&con->resp_cond, &con->resp_mtx);
	}
}


