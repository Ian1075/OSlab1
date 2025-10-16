#include "receiver.h"

// 鍵值必須與 sender 的定義完全相同
#define MSG_QUEUE_KEY 1234
#define SHM_KEY 5678

// 共享結構的定義也必須與 sender 完全相同
typedef struct {
    sem_t sender_sem;
    sem_t receiver_sem;
    char message_text[1024];
} shm_payload_t;

// 全域變數以符合 `receive` 函式的骨架
static double total_recv_time = 0.0;
static shm_payload_t *shm_ptr = NULL;

void receive(message_t* message_ptr, mailbox_t* mailbox_ptr) {
    struct timespec start, end;

    // 關鍵同步點 1: 等待 sender 發送訊息。
    // 如果 sender_sem 的值為 0，此處會阻塞，直到 sender 執行 sem_post。
    if (sem_wait(&shm_ptr->sender_sem) == -1) {
        perror("sem_wait sender_sem failed");
        exit(EXIT_FAILURE);
    }

    // 開始計時
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        // --- 訊息傳遞模式 ---
        // 從核心的訊息佇列中複製資料到使用者空間的 message_ptr
        if (msgrcv(mailbox_ptr->storage.msqid, message_ptr, sizeof(message_ptr->msgText), 1, 0) == -1) {
            perror("msgrcv failed");
            exit(EXIT_FAILURE);
        }
    } else { // SHARED_MEM
        // --- 共享記憶體模式 ---
        // 從共享記憶體中複製資料
        strcpy(message_ptr->msgText, shm_ptr->message_text);
    }
    
    // 停止計時
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_recv_time += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
    
    // 關鍵同步點 2: 通知 sender，訊息已處理完畢，你可以發送下一則了。
    if (sem_post(&shm_ptr->receiver_sem) == -1) {
        perror("sem_post receiver_sem failed");
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

    // --- 連接到由 sender 創建的 IPC 資源 ---
    // 注意：receiver 不使用 IPC_CREAT，因為它假設資源已經被 sender 創建好了。
    shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666);
    if (shmid == -1) {
        perror("shmget failed. Is sender running?");
        exit(EXIT_FAILURE);
    }
    shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666);
        if (msqid == -1) {
            perror("msgget failed. Is sender running?");
            exit(EXIT_FAILURE);
        }
        mailbox.storage.msqid = msqid;
    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        mailbox.storage.shm_addr = (char *)shm_ptr;
    } else {
        fprintf(stderr, "無效的機制選項。\n");
        shmdt(shm_ptr); // 離開前脫離
        exit(EXIT_FAILURE);
    }
    
    // --- 主迴圈：接收並處理訊息 ---
    while (1) {
        receive(&msg, &mailbox);
        
        // 檢查是否為結束訊息
        if (strcmp(msg.msgText, "EXIT\n") == 0) {
            printf("Sender exit!\n");
            break; // 收到結束信號，離開迴圈
        }
        
        printf("Receiving message: %s", msg.msgText);
        // 在 receiver 中，訊息是一個接一個處理的，stdout 緩衝區通常不是問題
        // 但如果遇到奇怪的顯示問題，加上 fflush(stdout) 是一個很好的除錯手段
    }

    // --- 輸出結果並清理 ---
    printf("Total time taken in receiving msg: %.9f s\n", total_recv_time);

    // receiver 的職責只是脫離共享記憶體。
    // 資源的最終移除工作應該由 sender (創建者) 來完成。
    shmdt(shm_ptr);

    return 0;
}