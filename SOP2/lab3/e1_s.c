#define _GNU_SOURCE
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define BACKLOG 3
volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig)
{
	do_work = 0;
}

int sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

int make_socket(int domain, int type)
{
	int sock;
	sock = socket(domain, type, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

int bind_tcp_socket(uint16_t port)
{
	struct sockaddr_in addr;
	int socketfd, t = 1;
	socketfd = make_socket(PF_INET, SOCK_STREAM);
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
		ERR("setsockopt");
	if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		ERR("bind");
	if (listen(socketfd, BACKLOG) < 0)
		ERR("listen");
	return socketfd;
}

int add_new_client(int sfd)
{
	int nfd;
	if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno)
			return -1;
		ERR("accept");
	}
	return nfd;
}

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s port\n", name);
}

ssize_t b_read(int fd, int32_t* buf)
{
	ssize_t c;
	c = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(int32_t)));
	return c;
}

ssize_t b_write(int fd, int32_t* buf)
{
	size_t c;
	c = TEMP_FAILURE_RETRY(write(fd, buf, sizeof(int32_t)));
	return c;
}


void communicate(int cfd, int32_t* maxnum)
{
	ssize_t size;
	int32_t data, num;
	if ((size = b_read(cfd, &data)) < 0)
		ERR("read:");
	if (size == (int)sizeof(int32_t)) {
		num = ntohl(data);
		printf("[S] Got %d from client\n", num);
		if(num > *maxnum)
			*maxnum = num;
		data = htonl(*maxnum);
		if (b_write(cfd, &data) < 0 && errno != EPIPE)
			ERR("write:");
	}
	if (TEMP_FAILURE_RETRY(close(cfd)) < 0)
		ERR("close");
}

void doServer(int fdT)
{
	int cfd, fdmax;
	int32_t maxnum = 0;
	fd_set base_rfds, rfds;
	sigset_t mask, oldmask;
	FD_ZERO(&base_rfds);

	FD_SET(fdT, &base_rfds);
	fdmax = fdT;
	
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);
	while (do_work) {
		rfds = base_rfds;
		if (pselect(fdmax + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0) {
			cfd = add_new_client(fdT);
			if (cfd >= 0)
				communicate(cfd, &maxnum);
		} else {
			if (EINTR == errno)
				continue;
			ERR("pselect");
		}
		do_work = 0;
	}
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char **argv)
{
	int fdT;
	int new_flags;
	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:");
	if (sethandler(sigint_handler, SIGINT))
		ERR("Seting SIGINT:");

	fdT = bind_tcp_socket(atoi(argv[1]));
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);
	doServer(fdT);

	if (TEMP_FAILURE_RETRY(close(fdT)) < 0)
		ERR("close");
	fprintf(stderr, "Server has terminated.\n");
	return EXIT_SUCCESS;
} 