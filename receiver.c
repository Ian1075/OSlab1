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

    if (sem_wait(&shm_ptr->sender_sem) == -1) {
        perror("sem_wait");
        exit(EXIT_FAILURE);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        if (msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(message_ptr->msgText), 1, 0) == -1) {
            perror("msgrcv");
            exit(EXIT_FAILURE);
        }
    } else {
        strcpy(message_ptr->msgText, shm_ptr->message_text);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_recv_time += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    
    if (sem_post(&shm_ptr->receiver_sem) == -1) {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "wtf\n");
        exit(EXIT_FAILURE);
    }

    int mechanism = atoi(argv[1]);
    mailbox_t mailbox;
    mailbox.flag = mechanism;
    message_t msg;

    int shmid = -1, msqid = -1;

    shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666);
    if (shmid == -1) {
        exit(EXIT_FAILURE);
    }
    shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666);
        if (msqid == -1) {
            shmdt(shm_ptr);
            exit(EXIT_FAILURE);
        }
        mailbox.storage.msqid = msqid;
    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        mailbox.storage.shm_addr = (char *)shm_ptr;
    } else {
        fprintf(stderr, "wtf\n");
        shmdt(shm_ptr);
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        receive(&msg, &mailbox);
        
        if (strcmp(msg.msgText, "EXIT\n") == 0) {
            printf("\nSender exit!\n");
            break;
        }
        
        printf("Receiving message: %s", msg.msgText);
    }

    printf("\nTotal time taken in receiving msg: %.9f s\n", total_recv_time);

    shmdt(shm_ptr);

    return 0;
}