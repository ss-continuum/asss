
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <paths.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <scheme.h>

#include "elderd.h"
#include "ooputils.h"

/*
 * a note about naming
 *
 * the scheme interpreter that this thing embeds is called MzScheme.
 * if you look at the rest of their projects at:
 * http://www.cs.rice.edu/CS/PLT/packages/
 * you can see they have a strange theme to their names: all of the
 * programs get "titles" like people would. so I tried to think of a
 * title to add to "daemon" because "scheme daemon" sounds dumb. so I
 * came up with "elder daemon", which sounds slightly cooler.
 *
 */

/* util options */

#define NOTHREAD
#define NOMPQUEUE

#include "util.h"


#define VERSION "0.1"
#define DEFAULT_LOG_FILE "elderd.log"


/* prototypes */

void run_child(int);
void install_primitives();


/* globals */

Scheme_Env *global;
FILE *logfile;
int childcount = 0;


/* functions */

void die_with_error(char *s)
{
	perror(s);
	if (errno >= 0 && errno < sys_nerr)
		fprintf(logfile, "fatal error: (%i) %s: %s\n", getpid(), s, sys_errlist[errno]);
	else
		fprintf(logfile, "fatal error: (%i) %s: <errno out of range>\n", getpid(), s);
	exit(1);
}


void open_log_file(char *path)
{
	logfile = fopen(path, "a");
	if (!logfile) die_with_error("elderd: open_log_file: fopen");
}


void log(const char *fmt, ...)
{
	va_list args;
	time_t tt;
	char *tstr;

	tt = time(NULL);
	tstr = ctime(&tt);
	tstr[20] = 0;

	fputs(tstr, logfile);
	va_start(args, fmt);
	vfprintf(logfile, fmt, args);
	va_end(args);
	fputs("\n", logfile);
	fflush(logfile);
}


int open_listening_port(unsigned short port)
{
	int sock, ret;
	struct sockaddr_in sin;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		die_with_error("open_listening_port: socket");

	memset(&sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	ret = bind(sock, &sin, sizeof(sin));
	if (ret == -1)
		die_with_error("open_listening_port: bind");

	ret = listen(sock, 5);
	if (ret == -1)
		die_with_error("open_listening_port: listen");

	return sock;
}


int daemonize(int noclose)
{
	int fd;

	switch (fork())
	{
		case -1:
			return -1;
		case 0:
			break;
		default:
			_exit(0);
	}

	if (setsid() == -1) return -1;
	if (noclose) return 0;

	fd = open(_PATH_DEVNULL, O_RDWR, 0);
	if (fd != -1)
	{
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2) close(fd);
	}
	return 0;
}


int main(int argc, char *argv[])
{
	int sock, newsock;

	/* ignore these signals */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	printf("Elder Daemon " VERSION "\n");
	printf("Logging to " DEFAULT_LOG_FILE "\n");
	printf("Daemonizing... (use -n to prevent)\n");

	if (argc < 2 || strcmp(argv[1], "-n"))
		daemonize(0);

	open_log_file(DEFAULT_LOG_FILE);

	log("Elder Daemon " VERSION " starting");
	log("Initializing Scheme environment");
	
	global = scheme_basic_env();
	install_primitives();

	log("Opening listening socket");

	sock = open_listening_port(ELDERDPORT);

	log("Listening on port %i", ELDERDPORT);

	for ( ; ; )
	{
		int sinsize = sizeof(struct sockaddr_in), pid;
		struct sockaddr_in sin;

		newsock = accept(sock, &sin, &sinsize);

		childcount++;

		if (newsock == -1)
		{
			die_with_error("elderd: main: accept");
			continue;
		}

		log("Accepted connection from %s:%i", inet_ntoa(sin.sin_addr),
				ntohs(sin.sin_port));

		pid = fork();

		if (pid == -1)
		{
			die_with_error("elderd: main: fork");
		}
		else if (pid == 0)
		{
			/* child process */
			close(sock);
			run_child(newsock);
			close(newsock);
			_exit(0);
		}
		else
		{
			/* parent process */
			close(newsock);
		}
	}

	return 0;
}


/**********************************************************************\
 *
 *  CHILD PROCESS
 *
\**********************************************************************/


/* child prototypes */

void send_text_message(int pid, char *msg);
void * listen_for_packet(int type);


/* child global data */

LinkedList evalqueue;
int sck;
struct data_a2e_playerdata *cachedpd;

struct {
	unsigned expressions, errors;
	unsigned cachehits, cachemisses;
	unsigned outoforder, toobigpackets;
} global_stats;


/* child main */

void run_child(int __s)
{
	struct data_a2e_evalstring *eval;
	char *msgbuf;

	/* initialize */
	sck = __s;
	LLInit(&evalqueue);
	msgbuf = scheme_malloc_atomic(100);

	/* enter loop */
	for ( ; ; )
	{
		/* nice to do each iteration */
		scheme_collect_garbage();

		/* check if there are any expressions in the queue */
		eval = LLRemoveFirst(&evalqueue);
		if (eval)
		{
			Scheme_Object *res;

			if (scheme_setjmp(scheme_error_buf))
			{
				/* error caught */
				global_stats.errors++;
				if (eval->pid >= 0)
				{
					send_text_message(eval->pid, "Error in Scheme expression");
				}
			}
			else
			{
				log("Evaluating string: %s", eval->string);
				global_stats.expressions++;

				res = scheme_eval_string(eval->string, global);
				if (eval->pid >= 0)
				{
					sprintf(msgbuf, "Scheme: %s", scheme_display_to_string_w_max(res, NULL, 80));
					send_text_message(eval->pid, msgbuf);
				}
			}
			eval = NULL;
			cachedpd = NULL; /* only cache for duration of one top-level eval */
		}
		else
		{
			/* try some network listening */
			listen_for_packet(A2E_EVALSTRING);
		}
	}
}


void * listen_for_packet(int reqtype)
{
	int size, bytes;
	void *msg;

	for ( ; ; )
	{
		bytes = read_full(sck, &size, sizeof(int));
		if (bytes == 0)
		{
			/* connection closed, exit */
			_exit(0);
		}
		else if (size > MAX_MESSAGE_SIZE)
		{
			char temp[1024];
			log("Recieved packet that's too big: %i", size);
			global_stats.toobigpackets++;
			/* read it all out */
			do {
				read_full(sck, temp, 1024);
				size -= 1024;
			} while (size > 1024);
			read_full(sck, temp, size);
		}
		else
		{
			msg = scheme_malloc_atomic(size);
			bytes = read_full(sck, msg, size);
			if (bytes == 0)
			{
				_exit(0);
			}
			else
			{
				int type;

				type = *(int*)msg;

				/* log("Got packet: %i", type); */

				if (type == A2E_EVALSTRING) /* queue it */
					LLAdd(&evalqueue, msg);
				if (type == A2E_PLAYERDATA) /* cache it */
					cachedpd = msg;

				if (type == reqtype)
					return msg;
				else if (type != A2E_EVALSTRING)
				{
					log("Out of order packet recieved: %i", type);
					global_stats.outoforder++;
				}
			}
		}
	}
}


/* new scheme primitives for asss */

Scheme_Object * prim_test(int argc, Scheme_Object **argv)
{
	return scheme_make_string("fooo!");
}

Scheme_Object * prim_findplayer(int argc, Scheme_Object **argv)
{
	struct data_e2a_findplayer *msg;
	struct data_a2e_playerdata *pd;
	char *name;
	int len;
	
	if (!SCHEME_STRINGP(argv[0]))
		return scheme_make_integer(-1);

	name = SCHEME_STR_VAL(argv[0]);
	len = SCHEME_STRLEN_VAL(argv[0]) + sizeof(struct data_e2a_findplayer);

	msg = scheme_malloc_atomic(len);
	msg->type = E2A_FINDPLAYER;
	strcpy(msg->name, name);
	write_message(sck, msg, len);
	msg = NULL;

	pd = listen_for_packet(A2E_PLAYERDATA);
	return scheme_make_integer(pd->pid);
}


Scheme_Object * prim_sendmessage(int argc, Scheme_Object **argv)
{
	if (SCHEME_INTP(argv[0]) && SCHEME_STRINGP(argv[1]))
		send_text_message(SCHEME_INT_VAL(argv[0]), SCHEME_STR_VAL(argv[1]));
	return scheme_void;
}

/* end of new primitives */

void install_primitives()
{
#define ADD_PRIM(fname, name, mina, maxa) \
	scheme_add_global(name, scheme_make_prim_w_arity(prim_##fname, name, mina, maxa), global)

	ADD_PRIM(test, "test", 0, 0);
	ADD_PRIM(findplayer, "find-player", 1, 1);
	ADD_PRIM(sendmessage, "send-message", 2, 2);
#undef ADD_PRIM
}


void send_text_message(int pid, char *msg)
{
	/* log("send_text_message to %i: %s", pid, msg); */
	if (pid != -1)
	{
		struct data_e2a_sendmessage *pkt;
		int size;

		size = sizeof(struct data_e2a_sendmessage) + strlen(msg);
		pkt = scheme_malloc_atomic(size);
		pkt->type = E2A_SENDMESSAGE;
		pkt->pid = pid;
		strcpy(pkt->message, msg);
		write_message(sck, pkt, size);
	}
}


