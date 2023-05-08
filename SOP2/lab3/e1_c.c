#define _GNU_SOURCE

#include <stdint.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define TRIES 3
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

int make_socket(void)
{
	int sock;
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

struct sockaddr_in make_address(char *address, char *port)
{
	int ret;
	struct sockaddr_in addr;
	struct addrinfo *result;
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;
	if ((ret = getaddrinfo(address, port, &hints, &result))) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}
	addr = *(struct sockaddr_in *)(result->ai_addr);
	freeaddrinfo(result);
	return addr;
}

int connect_socket(char *name, char *port)
{
	struct sockaddr_in addr;
	int socketfd;
	socketfd = make_socket();
	addr = make_address(name, port);
	if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
		if (errno != EINTR)
			ERR("connect");
		else {
			fd_set wfds;
			int status;
			socklen_t size = sizeof(int);
			FD_ZERO(&wfds);
			FD_SET(socketfd, &wfds);
			if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &wfds, NULL, NULL)) < 0)
				ERR("select");
			if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
				ERR("getsockopt");
			if (0 != status)
				ERR("connect");
		}
	}
	return socketfd;
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


void usage(char *name)
{
	fprintf(stderr, "USAGE: %s domain port \n", name);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	int fd;
	int32_t data, num_sent, num_got;

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:");
	fd = connect_socket(argv[1], argv[2]);

	srand(getpid());

	num_sent = rand() % 1000 + 1;
	data = htonl(num_sent);

	if (b_write(fd, &data) < 0)
		ERR("write:");
	if (b_read(fd, &data) < (int)sizeof(int32_t))
		ERR("read:");

	num_got = ntohl(data);

	printf("[%d] Got %d from server\n", getpid(), num_got);
	if(num_got == num_sent)
		printf("[%d] HIT\n", getpid());


	if (TEMP_FAILURE_RETRY(close(fd)) < 0)
		ERR("close");
	return EXIT_SUCCESS;
}