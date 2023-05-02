#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                                                                                    \
	(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

// MAX_BUFF must be in one byte range
#define MAX_BUFF 200
#define LENGTH 10
#define MIN_VAL -10
#define MAX_VAL 10

volatile sig_atomic_t last_signal = 0;

int sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

void sig_handler(int sig)
{
	last_signal = sig;
}

void sigchld_handler(int sig)
{
	pid_t pid;
	for (;;) {
		pid = waitpid(0, NULL, WNOHANG);
		if (0 == pid)
			return;
		if (0 >= pid) {
			if (ECHILD == errno)
				return;
			ERR("waitpid:");
		}
	}
}

int random_number(){
    return rand() % (MAX_VAL - MIN_VAL + 1) + MIN_VAL;
}

void child_work(int in, int out)
{
    // ETAP1
    // printf("[%d] im alive with in: [%d], out: [%d]\n", getpid(), in, out);
    // sleep(1);

    // ETAP 2
    // char buf[2];
    // srand(getpid());
    // int k = rand() % 100;
    // buf[0] = k;
    // if(TEMP_FAILURE_RETRY(write(out, buf, 2)) < 0){
    //     printf("%d\n", getpid());
    //     ERR("write");
    // }

    // printf("[%d] wrote %d\n", getpid(), buf[0]);
    
    // if(TEMP_FAILURE_RETRY(read(in, buf, 2)) < 0)
    //     ERR("read");
    
   // printf("[%d] read %d\n", getpid(), buf[0]);
    srand(getpid());
    int status; char buf[LENGTH]; int len; char str[LENGTH]; int k;

    while(last_signal != SIGINT)
    {
        status = read(in, buf, LENGTH);
        if(status < 0 && errno == EINTR) return;
        if(status < 0)
            ERR("READ");
        if(status == 0) return;

        len = buf[0]; 
        memcpy(str, buf+1, len+1);

        printf("[%d] read: %s\n", getpid(), str);

        k = atoi(str);
        if(k == 0) return;
        k = random_number();

        sprintf(str, "%d", k);
        len = strlen(str);
        buf[0] = len;
        memcpy(buf+1, str, len);

        memset(buf + len + 1, 0, LENGTH - len - 1);

        //if(buf[0] == 0) return;

        status = TEMP_FAILURE_RETRY(write(out, buf, LENGTH));
        
        if(status < 0 && errno == EINTR) return;
        if(status < 0)
            ERR("WRITE");
        if(status == 0) return;

        memset(buf, 0, LENGTH - 1);
        memset(str, 0, LENGTH - 1);
    }
}

void parent_work(int in, int out)
{
    // ETAP1
    // printf("[PARENT] is working\n");

    // ETAP2
    // sleep(1);
    // child_work(in, out);

    srand(getpid());
    int status; char buf[LENGTH]; int len; char str[LENGTH]; int k;
    buf[0] = 1;
    buf[1] = '1';
    memset(buf + 2, 0, LENGTH - 2);

    if(TEMP_FAILURE_RETRY(write(out, buf, LENGTH)) < 0)
            ERR("Write");
    
    while(last_signal != SIGINT)
    {
        status = read(in, buf, LENGTH);
        if(status < 0 && errno == EINTR) return;
        if(status < 0)
            ERR("READ");
        if(status == 0) return;

        len = buf[0]; 
        memcpy(str, buf+1, len+1);
        printf("[PARENT] read %s\n", str);

        k = atoi(str);
        if(k == 0) return;
        k = random_number();

        sprintf(str, "%d", k);
        len = strlen(str);
        buf[0] = len;
        memcpy(buf+1, str, len);

        memset(buf + len + 1, 0, LENGTH - len - 1);

        status = TEMP_FAILURE_RETRY(write(out, buf, LENGTH));

        if(status < 0 && errno == EINTR) return;
        if(status < 0)
            ERR("WRITE");
        if(status == 0) return;

        memset(buf, 0, LENGTH - 1);
        memset(str, 0, LENGTH - 1);

    }
}

int create_children_and_pipes(int n, int mpipe[2])
{
	int childpipe[2];
    int in = mpipe[0], out = -1, previn = -1;
	int max = n; 

	while (n) {

       if (out >= 0 && TEMP_FAILURE_RETRY(close(childpipe[1])))
			ERR("close");
		//else if(out >= 0) printf("[PARENT] closes pipe [%d]\n", childpipe[1]);

		if (pipe(childpipe))
			ERR("pipe");

        //printf("[PARENT] creates pipe [%d][%d]\n",  childpipe[0], childpipe[1]);
        out = childpipe[1];
        //childpipe[0] = prevpipe;

		switch (fork()) {
		case 0:
			if (TEMP_FAILURE_RETRY(close(mpipe[1])))
				ERR("close");
            //printf("[%d] closes pipe [%d]\n", getpid(), mpipe[1]);

            if(n != max){
                if (TEMP_FAILURE_RETRY(close(mpipe[0])))
				    ERR("close");
                //printf("[%d] closes pipe [%d]\n", getpid(), mpipe[0]);
            }
            if (TEMP_FAILURE_RETRY(close(childpipe[0]))){
				ERR("close");
            }

            //printf("[%d] closes pipe [%d]\n", getpid(), childpipe[0]);

			child_work(in, out);

			if (TEMP_FAILURE_RETRY(close(in)))
				ERR("close");
            if(TEMP_FAILURE_RETRY(close(out)))
                ERR("close");
            //printf("[%d] closes pipe [%d] [%d]\n", getpid(), in, out);
			exit(EXIT_SUCCESS);

		case -1:
			ERR("Fork:");
		}

        previn = in;
        in = childpipe[0];

        
        if (previn >= 0 && previn != mpipe[0] && TEMP_FAILURE_RETRY(close(previn)))
            ERR("close");
            //printf("[PARENT] closes pipe [%d]\n", previn);
        
        if(n==1){
            // if (in >= 0 && in != mpipe[0] && TEMP_FAILURE_RETRY(close(in)))
			//     ERR("close");
            if (out >= 0 && TEMP_FAILURE_RETRY(close(out)))
			    ERR("close");
            return childpipe[0];
        }
        n--;
	}
    return -1;
}

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s n\n", name);
	fprintf(stderr, "0<n<=10 - number of children\n");
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{   
    // char buf[10];
    // sprintf(buf, "%d", -500);
    // printf("%s %d", buf, strlen(buf));
    
	int mpipe[2], n = 5, in, out;

    sethandler(sig_handler, SIGINT);
    sethandler(SIG_IGN, SIGPIPE);
    sethandler(sigchld_handler, SIGCHLD);
    
    if (pipe(mpipe))
		ERR("pipe");

    //printf("[PARENT] creates pipe [%d][%d]\n",  mpipe[0], mpipe[1]);
    out = mpipe[1];
	in = create_children_and_pipes(n, mpipe);


    if (TEMP_FAILURE_RETRY(close(mpipe[0])))
		ERR("close");
    //printf("[PARENT] closes pipe [%d]\n", mpipe[0]);

	parent_work(in, out);

    // while (wait(NULL) > 0)
    // ;

	if (TEMP_FAILURE_RETRY(close(in)))
		ERR("close");
    if (TEMP_FAILURE_RETRY(close(out)))
		ERR("close");
	//printf("[PARENT] closes pipe [%d] [%d]\n", in, out);


	return EXIT_SUCCESS;
}