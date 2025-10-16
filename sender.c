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

    if (sem_wait(&shm_ptr->receiver_sem) == -1) {
        perror("send: sem_wait(receiver_sem)");
        exit(EXIT_FAILURE);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        if (msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.msgText), 0) == -1) {
            perror("send: msgsnd");
            exit(EXIT_FAILURE);
        }
    } else {
        strcpy(shm_ptr->message_text, message.msgText);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    total_send_time += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;

    if (sem_post(&shm_ptr->sender_sem) == -1) {
        perror("send: sem_post(sender_sem)");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "用法: ./sender <mechanism> <input_file>\n");
        fprintf(stderr, "mechanism: 1 for Message Passing, 2 for Shared Memory\n");
        exit(EXIT_FAILURE);
    }

    int mechanism = atoi(argv[1]);
    const char *input_filename = argv[2];
    FILE *fp = fopen(input_filename, "r");
    if (!fp) {
        perror("sender: fopen");
        exit(EXIT_FAILURE);
    }

    mailbox_t mailbox;
    mailbox.flag = mechanism;
    message_t msg;
    msg.mType = 1;
    
    int shmid = -1, msqid = -1;

    shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("sender: shmget");
        exit(EXIT_FAILURE);
    }
    shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("sender: shmat");
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666 | IPC_CREAT);
        if (msqid == -1) {
            perror("sender: msgget");
            shmdt(shm_ptr);
            shmctl(shmid, IPC_RMID, NULL);
            exit(EXIT_FAILURE);
        }
        mailbox.storage.msqid = msqid;
    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        mailbox.storage.shm_addr = (char *)shm_ptr;
    } else {
        fprintf(stderr, "無效的機制選項。\n");
        shmdt(shm_ptr);
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    if (sem_init(&shm_ptr->sender_sem, 1, 0) == -1 || sem_init(&shm_ptr->receiver_sem, 1, 1) == -1) {
        perror("sender: sem_init");
        shmdt(shm_ptr);
        shmctl(shmid, IPC_RMID, NULL);
        if (msqid != -1) msgctl(msqid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    
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

    // [強化同步] 等待 receiver 確認收到 EXIT 訊息，這是防止競爭條件的關鍵
    printf("Waiting for receiver to acknowledge exit...\n");
    if (sem_wait(&shm_ptr->receiver_sem) == -1) {
        perror("sender: Final sem_wait");
    }
    printf("Receiver acknowledged. Cleaning up resources.\n");

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