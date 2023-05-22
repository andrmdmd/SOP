#define _GNU_SOURCE
#include <netinet/in.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

#define BACKLOG 3
#define MAX_CLIENTS 3

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define HERR(source) (fprintf(stderr, "%s(%d) at %s:%d\n", source, h_errno, __FILE__, __LINE__), exit(EXIT_FAILURE))

volatile sig_atomic_t do_work = 1;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
}

typedef struct thread_args{
    int id;
    pthread_t *tids;
    int client_fd;
    pthread_barrier_t *barrier;
    pthread_cond_t *cond_var;
    pthread_mutex_t *cond_mtx;
    int *last_id;
    char *last_letter;
    sem_t *sem;
    int *thread_count;
    pthread_mutex_t *thread_count_mtx;
} thread_args_t;

void sigint_handler(int sigNo);

void do_server(int server_fd);

// thread
void *handle_client(void* arg);
void first_stage(thread_args_t* args);
void second_stage(thread_args_t* args);

// cleanups
void main_cleanup(void* arg);
void mtx_cleanup(void* arg);
void barrier_cleanup(void* arg);

int LOCAL_make_socket(char* name, int type, struct sockaddr_un *addr)
{
    int socketfd;
    if ((socketfd = socket(AF_UNIX, type, 0)) < 0)
        ERR("mysocklib: socket() error");

    // zero the structure
    memset(addr, 0, sizeof(struct sockaddr_un));

    // set socket's family to UNIX
    addr->sun_family = AF_UNIX;

    // set socket's name
    strncpy(addr->sun_path, name, sizeof(addr->sun_path) - 1);

    return socketfd;
}

int LOCAL_bind_socket(char* name, int type, int backlog)
{
    struct sockaddr_un addr;
    int socketfd;

    // bind will link the socket with the file
    // so we need to remove the file with the same name
    if (unlink(name) < 0 && errno != ENOENT)
        ERR("mysocklib: bind() error");
    
    socketfd = LOCAL_make_socket(name, type, &addr);

    // bind the socket
    if (bind(socketfd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0)
        ERR("mysocklib: bind() error");

    // start listening
    if (listen(socketfd, backlog))
        ERR("mysocklib: listen() error");

    return socketfd;
}

int LOCAL_connect_socket(char *name, int type)
{
	struct sockaddr_un addr;
	int socketfd;

	socketfd = LOCAL_make_socket(name, type, &addr);

    // add this client socket to the listen queue, may run asynchronously
    // sizeof would return bigger value than SUN_LEN (includes bytes of the gaps), 
    // but sizeof isn't considered as a mistake
    if (connect(socketfd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0) {
		if (EINTR != errno) {
			ERR("mysocklib: connect() error");
		} else {
			fd_set write_fd_set;
			int status;
			socklen_t size = sizeof(int);

            // initialize the set
			FD_ZERO(&write_fd_set);

            // add socketfd to the set of descriptors that we will be wating for
			FD_SET(socketfd, &write_fd_set);

            // we need to wait for the server to accept this connection (connect() may run async, for example if it 
            // is interrupted by the signal), so we have to monitor if socketfd inlcuded int write_fd_set is ready
            // MANPAGE: A file descriptor is considered ready if it is
            // possible to perform a corresponding I/O operation (e.g., read(2), or a sufficiently small write(2)) without blocking.
            // in the case of an error, pselect also returns.
            // we use select in both blocking and non-blocking states
			if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &write_fd_set, NULL, NULL)) < 0)
				ERR("mysocklib: select() error");

            // get status of the socket to indicate whether an error occured
			if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
				ERR("mysocklib: getsockopt() error");

            // check if error occured
			if (0 != status)
				ERR("mysocklib: connect() error");
		}
	}
	return socketfd;
}

struct sockaddr_in IPv4_make_address(char *address, char *port)
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
    //  printf("%s\n", inet_ntoa (addr.sin_addr));
	freeaddrinfo(result);
	return addr;
}

int TCP_IPv4_make_socket(void)
{
    int socketfd;
    if ((socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        ERR("mysocklib: socket() error");

    return socketfd;
}

int TCP_IPv4_bind_socket(uint16_t port, int backlog)
{
    struct sockaddr_in addr;
	int socketfd, status = 1;

    socketfd = TCP_IPv4_make_socket();

    // zero the structure
    memset(&addr, 0, sizeof(struct sockaddr_in));

    // set the structure
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // 0.0.0.0 address
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // without this option, system blocks access to the port for few minutes
    // we won't receive any remains from the previous connection on tcp (on upd we would receive them)
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &status, sizeof(status)))
        ERR("mysocklib: setsockopt() error");
    
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("mysocklib: bind() error");

    if (listen(socketfd, backlog))
        ERR("mysocklib: listen() error");

    return socketfd;
}

int TCP_IPv4_connect_socket(char *name, char *port)
{
    struct sockaddr_in addr;
	int socketfd;

    socketfd = TCP_IPv4_make_socket();
    addr = IPv4_make_address(name, port);

    if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
        if (EINTR != errno) {
            ERR("mysocklib: connect() error");
        } else {
			fd_set write_fd_set;
			int status;
			socklen_t size = sizeof(int);

            // initialize the set
			FD_ZERO(&write_fd_set);

            // add socketfd to the set of descriptors that we will be wating for
			FD_SET(socketfd, &write_fd_set);

            // we need to wait for the server to accept this connection (connect() may run async, for example if it 
            // is interrupted by the signal), so we have to monitor if socketfd inlcuded int write_fd_set is ready
            // MANPAGE: A file descriptor is considered ready if it is
            // possible to perform a corresponding I/O operation (e.g., read(2), or a sufficiently small write(2)) without blocking.
            // in the case of an error, pselect also returns.
            // we use select in both blocking and non-blocking states
			if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &write_fd_set, NULL, NULL)) < 0)
				ERR("mysocklib: select() error");

            // get status of the socket to indicate whether an error occured
			if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
				ERR("mysocklib: getsockopt() error");

            // check if error occured
			if (0 != status)
				ERR("mysocklib: connect() error");
        }
    }

    return socketfd;
}

int UDP_IPv4_make_socket(void)
{
    int socketfd;
    if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        ERR("mysocklib: socket() error");

    return socketfd;
}

int UDP_IPv4_bind_socket(uint16_t port)
{
    struct sockaddr_in addr;
	int socketfd, status = 1;

    socketfd = UDP_IPv4_make_socket();

    // zero the structure
    memset(&addr, 0, sizeof(struct sockaddr_in));

    // set the structure
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // 0.0.0.0 address
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // without this option, system blocks access to the port for few minutes
    // we won't receive any remains from the previous connection on tcp (on upd we would receive them)
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &status, sizeof(status)))
        ERR("mysocklib: setsockopt() error");
    
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("mysocklib: bind() error");

    return socketfd;
}

int add_new_client(int serverfd)
{
    int clientfd;

    // extract firt element in the server's queue of pending connections
    // and create a client socket
    if ((clientfd = TEMP_FAILURE_RETRY(accept(serverfd, NULL, NULL))) < 0) {

        // if serverfd is set to nonblock and there is no connection
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        
        // if error occured    
        ERR("mysocklib: accept() error");
    }

    return clientfd;
}
 
ssize_t bulk_read_always_block(int fd, char *buf, size_t count)
{
    int read_result;
    fd_set base_rfds; 
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);

    size_t len = 0;

    do {
        fd_set rfds = base_rfds;

        // required for a non-blocking state, select waits until the file is ready 
        // to perform read/write without blocking
        if (select(fd + 1, &rfds, NULL, NULL, NULL) < 0)
				ERR("mysocklib: select() error");

        read_result = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (read_result < 0)
            ERR("mysocklib: read() error");

        // handle end of file
        if (0 == read_result)
            return len;

        // move pointer
        buf += read_result;
        len += read_result;
        count -= read_result;
    } while (count > 0);
    
    return len;
}

ssize_t bulk_write_always_block(int fd, char *buf, size_t count)
{
    int write_result;
    fd_set base_wfds; 
    FD_ZERO(&base_wfds);
    FD_SET(fd, &base_wfds);

    size_t len = 0;
    
    do {
        fd_set wfds = base_wfds;

        // required for a non-blocking state, select waits until the file is ready 
        // to perform read/write without blocking
        if (select(fd + 1, NULL, &wfds, NULL, NULL) < 0)
				ERR("mysocklib: select() error");

        write_result = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (write_result < 0)
            ERR("mysocklib: write() error");

        // move pointer
        buf += write_result;
        len += write_result;
        count -= write_result;
    } while (count > 0);
    
    return len;
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
        // eof
		if (0 == c)
			return len;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(write(fd, buf, count));
		if (c < 0)
			return c;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}


int sethandler(void (*f)(int), int sig_no)
{
    struct sigaction act;

	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	
    if (-1 == sigaction(sig_no, &act, NULL))
		return -1;

	return 0;
}

void bulk_nanosleep(int sec, int nsec)
{
    struct timespec req, rem;
    req.tv_sec = sec;
    req.tv_nsec = nsec;

    while (nanosleep(&req, &rem) == -1 && EINTR == errno) {
        req = rem;
    }
}

int main(int argc, char** argv)
{
    int server_fd;
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Setting PIPE failed");

    if (sethandler(sigint_handler, SIGINT))
        ERR("Setting SIGINT failed");

    // non blocking mode tcp ipv4
    server_fd = TCP_IPv4_bind_socket(atoi(argv[1]), BACKLOG);
    int new_flags = fcntl(server_fd, F_GETFL) | O_NONBLOCK;
    if (fcntl(server_fd, F_SETFL, new_flags) == -1)
        ERR("fcntl");

    do_server(server_fd);

    // close the server
    if (TEMP_FAILURE_RETRY(close(server_fd)) < 0)
        ERR("Cannot close server_fd");

    fprintf(stderr, "[Server] Terminated\n");

    return EXIT_SUCCESS;
}

void sigint_handler(int sigNo)
{
    do_work = 0;
}

void do_server(int server_fd)
{
    pthread_t tids[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; ++i)
        tids[i] = -1;

    pthread_mutex_t  thread_count_mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t cond_mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;

    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, MAX_CLIENTS))
        ERR("pthread_barrier_init()");

    int thread_count = 0;
    int last_id = 0;
    char last_letter = 'A';

    // initialize unnamed semaphroe
    sem_t thrds_sem;
    if (sem_init(&thrds_sem,0,MAX_CLIENTS))
        ERR("sem_init()");

    // descriptors for pselect (pselect blocks in order to wait for incoming clients connections)
    fd_set base_rfds, rfds;
    FD_ZERO(&base_rfds);
    FD_SET(server_fd, &base_rfds);

    // SIGINT and SIGUSR1 will be ignoered if pselect is not running
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    fprintf(stderr, "[Server] Started\n");

    while (do_work) {
        rfds = base_rfds;
        if (pselect(server_fd + 1, &rfds, NULL, NULL, NULL, &oldmask) != -1) {

            // new client connection is pending
            int client_fd;
            if ((client_fd = add_new_client(server_fd)) == -1)
                continue;

            // order one thread for the client if it is possible
            if (sem_trywait(&thrds_sem) == -1) {
                if (errno == EAGAIN) {
                    // the semaphore was already locked
                    // send an information about full server to the client
                    char buff = '0';
                    if (TEMP_FAILURE_RETRY(send(client_fd, &buff, sizeof(char), 0))) {
                        // ignore error caused by situation when the client is disconnected
                        if (errno != EPIPE)
                            ERR("send()");
                    }
                    continue;
                } else {
                    ERR("sem_trywait()");
                }
            }

            // initialize arguments for the new thread handling a client
            thread_args_t *args;
            if ((args = (thread_args_t*)malloc(sizeof(thread_args_t))) == NULL)
                ERR("malloc()");
            args->client_fd = client_fd;
            args->sem = &thrds_sem;
            args->thread_count = &thread_count;
            args->thread_count_mtx = &thread_count_mtx;
            args->cond_mtx = &cond_mtx;
            args->cond_var = &cond_var;
            args->last_id = &last_id;
            args->last_letter = &last_letter;
            args->barrier = &barrier;
            args->tids = tids;

            // increment the thread count
            if (pthread_mutex_lock(args->thread_count_mtx))
                ERR("pthread_mutex_lock()");
            args->id = ++thread_count;

            if (pthread_mutex_unlock(args->thread_count_mtx))
                ERR("pthread_mutex_unlock()");

            // create a new thread for the client
            pthread_t tid = -1;
            if (pthread_create(&tid, NULL, handle_client, (void *)args))
                ERR("pthread_create");
            tids[args->id - 1] = tid;

            // detach the thread
            if (pthread_detach(tid))
                ERR("pthread_detach");


        } else if (errno != EINTR) {
                ERR("pselect()");
        }
    }

    if (sem_destroy(&thrds_sem))
        ERR("sem_destroy()");

    if (pthread_mutex_destroy(&thread_count_mtx))
        ERR("pthread_mutex_destroy()");

    if (pthread_mutex_destroy(&cond_mtx))
        ERR("pthread_mutex_destroy()");

    if (pthread_cond_destroy(&cond_var))
        ERR("pthread_cond_destroy()");
}

void *handle_client(void* arg)
{
    thread_args_t *args = (thread_args_t*) arg;

    // push the main cleanup
    pthread_cleanup_push(main_cleanup, (void *) args);

    /* FIRST STATE - WAITING FOR ALL THE CLIENTS */
    first_stage(args);

    pthread_cleanup_push(barrier_cleanup, (void *) args);

    pthread_cleanup_push(mtx_cleanup, (void *) args);

    /* SECOND STATE - ,,LITEROWANIE'' */
    second_stage(args);

    if (args->id == MAX_CLIENTS) {
        // it is the last thread - reset values
        // all the other threads are blocked at this point, so this access is valid
        *args->last_id = 0;
        *args->last_letter = 'A';
    } else {
        // wakeup the next thread
        pthread_cond_broadcast(args->cond_var);
    }

    // unlock the mutex by poping mtx_cleanup
    pthread_cleanup_pop(1);

    // call the barrier
    pthread_cleanup_pop(1);

    // to main cleanup
    pthread_cleanup_pop(1);

    return NULL;
}

void first_stage(thread_args_t* args)
{
    // control message
    char buff = '1';

    while (1) {
        // break the loop if there are MAX_CLIENTS clients
        if (pthread_mutex_lock(args->thread_count_mtx))
            ERR("pthread_mutex_lock()");
        if (*args->thread_count >= MAX_CLIENTS) {
            if (pthread_mutex_unlock(args->thread_count_mtx))
                ERR("pthread_mutex_unlock()");
            break;
        }
        if (pthread_mutex_unlock(args->thread_count_mtx))
            ERR("pthread_mutex_unlock()");

        // send control message to the client
        if (TEMP_FAILURE_RETRY(send(args->client_fd, &buff, sizeof(char), 0)) == -1) {
            // ignore error caused by situation when the client is disconnected
            if (errno == EPIPE) {
                // call the cleanup func and terminate the thread
                pthread_exit(NULL);
            } else {
                ERR("send()");
            }
        }

        // sleep 1 sec before the next notification
        bulk_nanosleep(1, 0);
    }
}

void second_stage(thread_args_t* args)
{
    if (pthread_mutex_lock(args->cond_mtx))
        ERR("pthread_mutex_lock()");

    while (*args->last_id + 1 != args->id) {
        // The application shall ensure that pthread_cond_wait function is called
        // with the mutex locked by the calling thread (manual)
        pthread_cond_wait(args->cond_var, args->cond_mtx);
    }

    *args->last_id = args->id;

    char letter = '0';
    char waiting_for = (char)((*args->last_letter + 1) % 'A' + 'A');

    fprintf(stderr, "[Server] Waiting for %c\n", waiting_for);

    if (TEMP_FAILURE_RETRY(send(args->client_fd, args->last_letter, sizeof(char), 0)) == -1) {
        // ignore error caused by situation when the client is disconnected (it will be checked in recv)
        if (errno != EPIPE)
            ERR("send()");
    }

    while (letter != waiting_for) {
        ssize_t size;
        if (TEMP_FAILURE_RETRY(size = (recv(args->client_fd, &letter, sizeof(char), 0))) == -1)
            ERR("recv()");

        if (size == 0) {
            // eof detected => cancel threads
            for (int i = 1; i <= MAX_CLIENTS; ++i) {
                if (i == args->id) continue;
                if (pthread_cancel(args->tids[i - 1]))
                    ERR("pthread_cancel()");
            }

            pthread_exit(NULL);
        }
    }

    *args->last_letter = letter;
}

void main_cleanup(void* arg)
{
    thread_args_t *args = (thread_args_t*) arg;

    fprintf(stderr, "[Server] Cleanup %d\n", args->id);

    // decrement the thread count
    if (pthread_mutex_lock(args->thread_count_mtx))
        ERR("pthread_mutex_lock()");
    (*args->thread_count)--;
    if (pthread_mutex_unlock(args->thread_count_mtx))
        ERR("pthread_mutex_unlock()");

    // resource (thread) should be available from now
    sem_post(args->sem);

    if (TEMP_FAILURE_RETRY(close(args->client_fd)))
        ERR("close()");

    free(args);

    printf("[Server] Thread finished succesfuly");
}

void mtx_cleanup(void* arg)
{
    thread_args_t *args = (thread_args_t*) arg;
    if (pthread_mutex_unlock(args->cond_mtx))
        ERR("pthread_mutex_unlock()");
}

void barrier_cleanup(void* arg)
{
    thread_args_t *args = (thread_args_t*) arg;
    fprintf(stderr, "[Server] On barrier - %d\n", args->id);
    // wait for all threads to reach that point
    int result;
    if ((result = pthread_barrier_wait(args->barrier))) {
        // the constant PTHREAD_BARRIER_SERIAL_THREAD shall be returned to
        // one  unspecified  thread  and  zero  shall  be returned to each of the remaining
        // threads (manual)
        if (result != PTHREAD_BARRIER_SERIAL_THREAD)
            ERR("pthread_barrier_wait()");
    }

    // NIE WIEM JAKIM CUDEM, ALE JAK NIE MA TEGO PRINTA TO NIE DZIALA
    fprintf(stderr, "[Server] Barrier end - %d\n", args->id);
}