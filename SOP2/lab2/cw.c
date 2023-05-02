#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source)                                                                                                    \
	(fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_QUEUES_COUNT 100
#define STDIN_MAX 128

typedef struct queue_info {
    mqd_t descriptor;
    int index;
    struct queue_info *all_queues;
    int all_queues_count;
} queue_info_t;

void usage(void)
{
	fprintf(stderr, "USAGE: \n");
	fprintf(stderr, "1<=n<=100\n");
	exit(EXIT_FAILURE);
}

void open_queues(queue_info_t queues[MAX_QUEUES_COUNT], int count)
{
    static char q_name[16];

    // prepare queues attributes
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 4;

    for(int i = 0; i < count; ++i) {  
        // prepare current name
        sprintf(q_name, "/sop2q_%d", i);

        // create
        if ((queues[i].descriptor = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1) {
            ERR("mq_open failed");
        }
        queues[i].index = i;
        queues[i].all_queues = queues;
        queues[i].all_queues_count = count;
        printf("Queue [%d] with a name \"%s\" has been created, fd=%d.\n", i, q_name, queues[i].descriptor);
    }
}

void close_and_unlink_queues(queue_info_t queues[MAX_QUEUES_COUNT], int count)
{
    static char q_name[16];

    for(int i = 0; i < count; ++i) {  
        if (TEMP_FAILURE_RETRY(mq_close(queues[i].descriptor)) == (mqd_t)-1) {
            ERR("mq_close failed");
        }
        printf("Queue [%d] with fd=%d has been closed.\n", i, queues[i].descriptor);
    }

    for(int i = 0; i < count; ++i) {  
        // prepare current name
        sprintf(q_name, "/sop2q_%d", i);

        // create
        if (mq_unlink(q_name)) {
            ERR("mq_unlink failed");
        }
        printf("Queue [%d] with a name \"%s\" has been unlinked.\n", i, q_name);
    }
}

void load_numbers(queue_info_t queues[MAX_QUEUES_COUNT], int count)
{
    static char buff[STDIN_MAX + 2];
	while (fgets(buff, STDIN_MAX + 2, stdin) != NULL) {
        char* endptr;
        errno = 0;
        strtol(buff, &endptr, 10);
        if (errno != 0) {
            perror("strtol");
            exit(EXIT_FAILURE);
        }

        if (endptr == buff) { // it is not a number
            continue;
        }

        uint32_t msg = atoi(buff);
        uint32_t index = rand() % count;

		printf("Input: %d has been send to the queue with fd=%d and id=%d\n", atoi(buff), queues[index].descriptor, queues[index].index);

        if (TEMP_FAILURE_RETRY(mq_send(queues[rand() % count].descriptor, (const char*)&msg, 4, 1))) {
            ERR("send failed");
        }
    }

    printf("Loading has been finished!\n");
}

void tfunc(union sigval sv)
{
    queue_info_t *info = (queue_info_t *) sv.sival_ptr;

    // set notification again
    static struct sigevent not;
    not.sigev_notify = SIGEV_THREAD;
    not.sigev_notify_function = tfunc; 
    not.sigev_notify_attributes = NULL;
    not.sigev_value.sival_ptr = info;
    if (mq_notify(info->descriptor, &not)) {
        ERR("mq_notify failed");
    }

    // empty the message queue
    for (;;) {
        uint32_t msg;
        unsigned int priority;
        if (mq_receive(info->descriptor, (char *)&msg, 4, &priority) < 1) {
            if (errno == EAGAIN) {
                // if the queue is empty
                break;
            } else {
                ERR("mq_receive failed");
            }
        }
        printf("Received: %d (fd=%d, id=%d)\n", msg, info->descriptor, info->index);

        if (info->index + 1 >= info->all_queues_count) {
            printf("Output: %d\n", msg);
            continue;
        }

        if (msg % (info->index + 1) == 0) {
            continue;
        }

        if (mq_send(info->all_queues[info->index + 1].descriptor, (char *)&msg, 4, 0) < 0) {
            ERR("mq_send failed");
        }
    }
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    if (argc != 2)
        usage();
    
    int n = atoi(argv[1]);

    if (n < 1 || n > MAX_QUEUES_COUNT)
        usage();

    queue_info_t queues[MAX_QUEUES_COUNT];

    open_queues(queues, n);
    sleep(2);
    close_and_unlink_queues(queues, n);
}

