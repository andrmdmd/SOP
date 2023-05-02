#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define LENGTH 10

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
void sigint_handler(int sig)
{
	last_signal=sig;
}
void child_work(int n, int read_, int write_)
{
    //int l=3;
    //char buf[3];
    // srand(getpid());
    // int k=rand()%100;
    // buf[0]=k;
    // memset(buf+1,0,l-1);
    // if(TEMP_FAILURE_RETRY(write(write_,buf,l))<0) ERR("write");
    // printf("[%d] written %d\n",getpid(),buf[0]);
    // if(TEMP_FAILURE_RETRY(read(read_,buf,l))<0) ERR("read");
    // printf("[%d] read %d\n",getpid(),buf[0]);
    srand(getpid());
    if(sethandler(sigint_handler, SIGINT))
        ERR("seting SIGINT");
    char buf[LENGTH];
    int status;
    while(last_signal!=SIGINT)
    {
        status=read(read_, buf, LENGTH);
        if(status<0&&errno==EINTR) return;
        if(status<0)
            ERR("READ");
        if(status==0) return;

        printf("[%d], received: %d\n", getpid(), buf[0]);

        if(buf[0]==0) return;
        int k=-10+rand()%20;
        buf[0]+=k;

        memset(buf+1, 0, LENGTH-1);
        status=TEMP_FAILURE_RETRY(write(write_, buf, LENGTH));
        if(status<0)
            ERR("Write");
        memset(buf, 0, LENGTH-1);
    }
}
void parent_work(int read_, int write_)
{
    //int l=3;
    //char buf[3];
    if(sethandler(sigint_handler, SIGINT))
        ERR("seting SIGINT");
    
    char buf[LENGTH];
    buf[0]=1;
    memset(buf+1,0,LENGTH-1);
    if(TEMP_FAILURE_RETRY(write(write_, buf, LENGTH)) < 0)
            ERR("Write");
    int status;
    while(last_signal!=SIGINT)
    {
        status=read(read_, buf, LENGTH);
        if(status<0&&errno==EINTR) return;
        if(status<0)
            ERR("READ");
        if(status==0) return;

        printf("[%d], received: %d\n", getpid(), buf[0]);

        if(buf[0]==0) return;
        int k=-10+rand()%20;
        buf[0]+=k;

        memset(buf+1, 0, LENGTH-1);
        status=TEMP_FAILURE_RETRY(write(write_, buf, LENGTH));

        if(status<0)
            ERR("Write");
        memset(buf, 0, LENGTH-1);
    }
}
void create_pipes(int n, int* p_d1, int* p_d2, int* d1_d2)
{
    if(pipe(p_d1)) ERR("pipe");
    if(pipe(p_d2)) ERR("pipe");
    if(pipe(d1_d2)) ERR("pipe");
    while(n)
    {
        switch(fork())
        {
            case 0:
            {
                if(n==2)
                {
                    if(TEMP_FAILURE_RETRY(close(p_d1[0]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(p_d1[1]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(p_d2[0]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(d1_d2[1]))) ERR("close");
                    //fprintf(stderr,"[child %d] closing %d,%d,%d,%d\n",n,p_d1[0],p_d1[1],p_d2[0],d1_d2[1]);
                    child_work(n,d1_d2[0],p_d2[1]);

                    if(p_d2[1]&&TEMP_FAILURE_RETRY(close(p_d2[1]))) ERR("close");
                    if(d1_d2[0]&&TEMP_FAILURE_RETRY(close(d1_d2[0]))) ERR("close");
                    printf("[%d] exiting\n",getpid());
                    exit(EXIT_SUCCESS);
                }
                if(n==1)
                {
                    if(TEMP_FAILURE_RETRY(close(p_d2[0]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(p_d2[1]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(p_d1[1]))) ERR("close");
                    if(TEMP_FAILURE_RETRY(close(d1_d2[0]))) ERR("close");
                    //fprintf(stderr,"[child %d] closing %d,%d,%d,%d\n",n,p_d2[0],p_d2[1],p_d1[1],d1_d2[0]);
                    child_work(n,p_d1[0],d1_d2[1]);

                    if(p_d1[0]&&TEMP_FAILURE_RETRY(close(p_d1[0]))) ERR("close");
                    if(d1_d2[1]&&TEMP_FAILURE_RETRY(close(d1_d2[1]))) ERR("close");
                    printf("[%d] exiting\n",getpid());
                    exit(EXIT_SUCCESS);
                }
            }
            case -1:
                ERR("fork");
        }
        n--;
    }
    parent_work(p_d2[0],p_d1[1]);
    if(d1_d2[0]&&TEMP_FAILURE_RETRY(close(d1_d2[0]))) ERR("close");
    if(d1_d2[1]&&TEMP_FAILURE_RETRY(close(d1_d2[1]))) ERR("close");
    if(p_d1[0]&&TEMP_FAILURE_RETRY(close(p_d1[0]))) ERR("close");
    if(p_d2[1]&&TEMP_FAILURE_RETRY(close(p_d2[1]))) ERR("close");
    printf("[parent] exiting\n");
    //fprintf(stderr,"[%d] closing %d,%d,%d,%d\n",getpid(),d1_d2[0],d1_d2[1],p_d1[0],p_d2[1]);

    

}


int main(int argc, char **argv)
{
    if (sethandler(sigchld_handler, SIGCHLD))
		ERR("Seting parent SIGCHLD:");
    if(sethandler(SIG_IGN, SIGPIPE))
        ERR("seting SIGPIPE");
    if(sethandler(SIG_IGN, SIGINT))
        ERR("seting SIGINT");
    
    int p_d1[2],p_d2[2],d1_d2[2];
    create_pipes(2,p_d1,p_d2,d1_d2);

    if(p_d1[1]&&TEMP_FAILURE_RETRY(close(p_d1[1]))) ERR("close");
    if(p_d2[0]&&TEMP_FAILURE_RETRY(close(p_d2[0]))) ERR("close");
    return EXIT_SUCCESS;
}
