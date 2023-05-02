#define _GNU_SOURCE
#include <sys/types.h>
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
#define LENGTH 16
#define MAX_M 10
#define MAX_N 5

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

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s N M\n", name);
	fprintf(stderr, "2<=N<=5 - number of children\n");
    fprintf(stderr, "5<=M<=10 - number of cards\n");
	exit(EXIT_FAILURE);
}

void child_work(int index, int in, int out, int m)
{
    int cards[MAX_M + 1]; int mr = m; char buf[LENGTH]; int k;
    srand(getpid());
    for(int i = 0; i < MAX_M; i++)
        cards[i] = 0;
    while(mr--){
        
        //printf("hello from %d %d\n", index, mr);
        if (TEMP_FAILURE_RETRY(read(in, buf, LENGTH)) < 0)
		    ERR("Read from parent");

        if(buf[0] == 'n'){
            if(rand()% 20 == 0){
                printf("[%d] dying\n", index);
                return;
            }

            memset(buf, 0, LENGTH);
            do{
                k = rand() % m + 1;
            }while(cards[k] != 0);
            cards[k] = 1;
            buf[0] = index;
            buf[1] = k;
            printf("[%d] sent %d to parent\n", index, k);
            memset(buf + 2, 0, LENGTH - 2);

            if (TEMP_FAILURE_RETRY(write(out, buf, LENGTH)) < 0)
                ERR("Write to parent");
        }

        if (TEMP_FAILURE_RETRY(read(in, buf, LENGTH)) < 0)
		    ERR("Read from parent");
        printf("[%d] Got %d points\n", index, buf[0]);
        memset(buf, 0, LENGTH);

    }
}

void parent_work(int m, int n, int **fds)
{
    int status; char buf[LENGTH]; int sentNum = 0, sent[MAX_N]; char nr[] = "new_round"; int max = 0; int winners = 0;
    int dead[MAX_N];
    for(int i = 0; i < MAX_N; i++){
            dead[i] = 0;
    }
    memcpy(buf, nr, strlen(nr));
    for(int r = 0; r < m; r++){

        printf("ROUND %d\n", r);
        for(int i = 0; i < MAX_N; i++){
            sent[i] = 0;
        }

        memcpy(buf, nr, strlen(nr));

        sentNum = 0;
        
        for(int i = 0; i < n; i++){
            if(dead[i] == 1) continue;
            printf("Parent sent to %d\n", i);
            if (TEMP_FAILURE_RETRY(write(fds[1][i], buf, LENGTH)) < 0)
			    ERR("write start round");
        }
        
        for(int i = 0; i < n; i++){
            if(dead[i] == 1) continue;

            memset(buf, 0, LENGTH);

            status = read(fds[0][i], buf, LENGTH);

            if(status == 0) {
                printf("[PARENT] player %d found dead\n", i);
                
                dead[i] = 1;
                sentNum++;
                continue;
            }

            printf("[PARENT] Got number %d from player %d\n", buf[1], buf[0]);
            sent[(int)buf[0]] = buf[1];
            sentNum++;
        }
        winners = 0;
        max = 0;
        for(int i = 0; i < n; i++){
            if(dead[i] == 1) continue;
            if(sent[i] > max){
                max = sent[i];
                winners = 1;
            }
            else if(sent[i] == max)
                winners++;
        }
        memset(buf, 0, LENGTH);
        for(int i = 0; i < n; i++){
            if(dead[i] == 1) continue;
            int point = 0;
            if(sent[i] == max){
               point = n/winners;
            }
            buf[0] = point;
            if (TEMP_FAILURE_RETRY(write(fds[1][i], buf, LENGTH)) < 0)
			    ERR("write");
        }
    }
}

// fds[0][i] - parent read from i
// fds[1][i] - parent write to i

void create_children_and_pipes(int n, int **fds, int m){
	
    int tmpfd_r[2], tmpfd_w[2];
	int max = n; int i = n-1;
	while (n) {
		if (pipe(tmpfd_r))
			ERR("pipe");
        if (pipe(tmpfd_w))
			ERR("pipe");
		switch (fork()) {
		case 0:
			while (n < max){
				if (fds[0][n] && TEMP_FAILURE_RETRY(close(fds[0][n])))
					ERR("close");
                if (fds[1][n] && TEMP_FAILURE_RETRY(close(fds[1][n++])))
					ERR("close");
            }

            free(fds[0]);
            free(fds[1]);
			free(fds);
            
			if (TEMP_FAILURE_RETRY(close(tmpfd_r[1])))
				ERR("close");
            if (TEMP_FAILURE_RETRY(close(tmpfd_w[0])))
				ERR("close");

			child_work(i, tmpfd_r[0], tmpfd_w[1], m);

			if (TEMP_FAILURE_RETRY(close(tmpfd_r[0])))
				ERR("close");
			if (TEMP_FAILURE_RETRY(close(tmpfd_w[1])))
				ERR("close");
			exit(EXIT_SUCCESS);

		case -1:
			ERR("Fork:");
		}
		if (TEMP_FAILURE_RETRY(close(tmpfd_r[0])))
			ERR("close");
        if (TEMP_FAILURE_RETRY(close(tmpfd_w[1])))
			ERR("close");
		fds[0][--n] = tmpfd_w[0];
        fds[1][n] = tmpfd_r[1];
        i--;
	}
}

int main(int argc, char **argv)
{
	int n, m, **fds; 

	if (argc != 3)
		usage(argv[0]);

	n = atoi(argv[1]);
    m = atoi(argv[2]);

	if (n < 2 || n > 5 || m < 5 || m > 10)
		usage(argv[0]);

	if (sethandler(SIG_IGN, SIGINT))
		ERR("Setting SIGINT handler");
	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Setting SIGINT handler");
	if (sethandler(sigchld_handler, SIGCHLD))
		ERR("Setting parent SIGCHLD:");

	if (NULL == (fds = (int **)malloc(sizeof(int*) * 2)))
		ERR("malloc");
    
    if (NULL == (fds[0] = (int *)malloc(sizeof(int) * n)))
		ERR("malloc");
    
    if (NULL == (fds[1] = (int *)malloc(sizeof(int) * n)))
		ERR("malloc");

	create_children_and_pipes(n, fds, m);

	parent_work(m, n, fds);
	while (n--){
		if (fds[0][n] && TEMP_FAILURE_RETRY(close(fds[0][n])))
			ERR("close");
        if (fds[1][n] && TEMP_FAILURE_RETRY(close(fds[1][n])))
			ERR("close");
    }

    free(fds[0]);
    free(fds[1]);
	free(fds);
	return EXIT_SUCCESS;
}