
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

#include <scheme.h>
#include "elderd.h"

#define NOTHREAD
#define NOMPQUEUE

#include "util.h"


#define VERSION "0.1"
#define DEFAULT_LOG_FILE "elderd.log"


/* prototypes */

void run_child(int);


/* globals */

Scheme_Env *global;
FILE *logfile;


/* functions */

void die_with_error(char *s)
{
	perror(s);
	fprintf(logfile, "die_with_error called: %s\n", s);
	exit(0);
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
		die_with_error("elderd: open_listening_port: socket");

	memset(&sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = INADDR_LOOPBACK;
	ret = bind(sock, &sin, sizeof(sin));
	if (ret == -1)
		die_with_error("elderd: open_listening_port: bind");

	ret = listen(sock, 5);
	if (ret == -1)
		die_with_error("elderd: open_listening_port: listen");

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

	printf("Elder Daemon " VERSION "\n");
	printf("Forking\n");

	if (argc < 2 || strcmp(argv[1], "-n"))
		daemonize(0);

	open_log_file(DEFAULT_LOG_FILE);

	log("Elder Daemon " VERSION " starting");
	log("Initializing Scheme environment");
	
	global = scheme_basic_env();

	log("Opening listening socket");

	sock = open_listening_port(ELDERDPORT);

	log("Listening on port %i", ELDERDPORT);

	for ( ; ; )
	{
		int sinsize = sizeof(struct sockaddr_in), pid;
		struct sockaddr_in sin;

		newsock = accept(sock, &sin, &sinsize);

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


/* child global data */

LinkedList *evalqueue;

/* child main */

void run_child(int sock)
{


}


void send_text_message(int pid, char *msg)
{
	if (pid != -1)
	{

	}
}

#if 0

int main()
{
	char *buf;
	Scheme_Object *res;

	global = scheme_basic_env();

	buf = scheme_malloc_atomic(1024);
	
	printf("> "); fflush(stdout);
	while (fgets(buf, 1024, stdin))
	{
		if (scheme_setjmp(scheme_error_buf))
		{
			printf("Error occured.\n");
		}
		else
		{
			res = scheme_eval_string(buf, global);
			printf("Result: %s\n", scheme_write_to_string(res, NULL));
		}
		printf("> "); fflush(stdout);
	}
	printf("\nExiting...\n");
	return 0;
}

#endif

