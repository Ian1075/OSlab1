#include "receiver.h"

#define MSG_QUEUE_KEY 1234
#define SHM_KEY 5678

typedef struct {
    sem_t sender_sem;
    sem_t receiver_sem;
    char message_text[1024];
} shm_payload_t;

static double total_recv_time = 0.0;
static shm_payload_t *shm_ptr = NULL;

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr) {
    struct timespec start, end;
    double time_taken;

    sem_wait(&shm_ptr->sender_sem);

    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(message_ptr->msgText), 1, 0)
    } else {
        strcpy(message_ptr->msgText, shm_ptr->message_text);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    total_recv_time += time_taken;

    sem_post(&shm_ptr->receiver_sem);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "用法: ./receiver <mechanism>\n");
        fprintf(stderr, "mechanism: 1 for Message Passing, 2 for Shared Memory\n");
        exit(EXIT_FAILURE);
    }

    int mechanism = atoi(argv[1]);
    mailbox_t mailbox;
    mailbox.flag = mechanism;
    message_t msg;
    int shmid, msqid;

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666);
        mailbox.storage.msqid = msqid;

        shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666);
        shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);

    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666);
        shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
        mailbox.storage.shm_addr = (char *)shm_ptr;

    }

    while (1) {
        receive(&msg, &mailbox);
        if (strcmp(msg.msgText, "EXIT\n") == 0) {
            printf("Sender exit!\n");
            break;
        }
        printf("Receiving message: %s", msg.msgText);
    }

    printf("Total time taken in receiving msg: %.9f s\n", total_recv_time);
    shmdt(shm_ptr);

    return 0;
}