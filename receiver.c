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
        perror("receive: sem_wait(sender_sem)");
        exit(EXIT_FAILURE);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        // [錯誤修正] 增加對 msgrcv 返回值的檢查
        if (msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(message_ptr->msgText), 1, 0) == -1) {
            // 如果 sender 提前清除了佇列，這裡會出錯。穩健的程式碼需要處理這種情況。
            // 在我們的強化同步模型下，這個錯誤理論上不應該再發生。
            perror("receive: msgrcv");
            // 發生這個錯誤時，我們不再 post 信號，直接退出，避免 sender 死鎖
            exit(EXIT_FAILURE);
        }
    } else {
        strcpy(message_ptr->msgText, shm_ptr->message_text);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_recv_time += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    
    if (sem_post(&shm_ptr->receiver_sem) == -1) {
        perror("receive: sem_post(receiver_sem)");
        exit(EXIT_FAILURE);
    }
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

    int shmid = -1, msqid = -1;

    // receiver 的職責是 "連接" 到已存在的資源
    shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666);
    if (shmid == -1) {
        perror("receiver: shmget. Is sender running first?");
        exit(EXIT_FAILURE);
    }
    shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("receiver: shmat");
        exit(EXIT_FAILURE);
    }

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666);
        if (msqid == -1) {
            perror("receiver: msgget. Is sender running first?");
            shmdt(shm_ptr);
            exit(EXIT_FAILURE);
        }
        mailbox.storage.msqid = msqid;
    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        mailbox.storage.shm_addr = (char *)shm_ptr;
    } else {
        fprintf(stderr, "無效的機制選項。\n");
        shmdt(shm_ptr);
        exit(EXIT_FAILURE);
    }
    
    while (1) {
        receive(&msg, &mailbox);
        
        // 收到 EXIT 訊息後，不再印出，直接跳出迴圈準備結束
        if (strcmp(msg.msgText, "EXIT\n") == 0) {
            printf("Sender exit!\n");
            break;
        }
        
        printf("Receiving message: %s", msg.msgText);
    }

    // [關鍵修正] 確保程式能執行到這裡
    printf("Total time taken in receiving msg: %.9f s\n", total_recv_time);

    // receiver 只需脫離共享記憶體即可，不負責刪除
    shmdt(shm_ptr);

    return 0;
}