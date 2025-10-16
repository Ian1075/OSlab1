#include "sender.h"

#define MSG_QUEUE_KEY 1234
#define SHM_KEY 5678
typedef struct {
    sem_t sender_sem;
    sem_t receiver_sem;
    char message_text[1024];
} shm_payload_t;

static double total_send_time = 0.0;
static shm_payload_t *shm_ptr = NULL;

void send(message_t message, mailbox_t* mailbox_ptr) {
    struct timespec start, end;
    double time_taken;

    sem_wait(&shm_ptr->receiver_sem);

    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.msgText), 0)
    } else {
        strcpy(shm_ptr->message_text, message.msgText);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    total_send_time += time_taken;

    sem_post(&shm_ptr->sender_sem);
}

int main(int argc, char *argv[]) {
    int mechanism = atoi(argv[1]);
    const char *input_filename = argv[2];
    FILE *fp = fopen(input_filename, "r");

    mailbox_t mailbox;
    mailbox.flag = mechanism;
    message_t msg;
    msg.mType = 1;
    int shmid, msqid;

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666 | IPC_CREAT);
        mailbox.storage.msqid = msqid;

        shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666 | IPC_CREAT);
        shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);

    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666 | IPC_CREAT);
        
        shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
        mailbox.storage.shm_addr = (char *)shm_ptr;

    }

    sem_init(&shm_ptr->sender_sem, 1, 0);
    sem_init(&shm_ptr->receiver_sem, 1, 1);

    char line_buffer[1024];
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        strcpy(msg.msgText, line_buffer);
        printf("Sending message: %s", msg.msgText);
        send(msg, &mailbox);
    }
    fclose(fp);

    printf("End of input file! exit!\n");
    strcpy(msg.msgText, "EXIT\n");
    send(msg, &mailbox);

    printf("Total time taken in sending msg: %.9f s\n", total_send_time);

    sem_destroy(&shm_ptr->sender_sem);
    sem_destroy(&shm_ptr->receiver_sem);
    shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);

    if (mechanism == MSG_PASSING) {
        msgctl(msqid, IPC_RMID, NULL);
    }

    return 0;
}