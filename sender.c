#include "sender.h"

// 為 IPC 物件定義唯一的鍵值，客戶端和伺服器端必須相同
#define MSG_QUEUE_KEY 1234
#define SHM_KEY 5678

// 定義一個共享結構來存放號誌和訊息。
// 這樣可以確保號誌和共享訊息使用同一個共享記憶體區段，簡化管理。
typedef struct {
    sem_t sender_sem;    // 用來通知 receiver：「嘿，有新訊息了！」
    sem_t receiver_sem;  // 用來等待 receiver：「我準備好接收下一則訊息了！」
    char message_text[1024]; // 共享記憶體模式下，訊息存放於此
} shm_payload_t;

// 使用全域變數來累積總時間，因為 send 函式介面沒有傳遞時間指標
static double total_send_time = 0.0;
// 將共享記憶體指標設為全域，方便在 send 和 main 中存取
static shm_payload_t *shm_ptr = NULL;

void send(message_t message, mailbox_t* mailbox_ptr) {
    struct timespec start, end;

    // 關鍵同步點 1: 等待 receiver 準備好。
    // 如果 receiver 還沒處理完上一則訊息，這裡會被阻塞。
    // 這是實現 "輪流" 機制的核心。
    if (sem_wait(&shm_ptr->receiver_sem) == -1) {
        perror("sem_wait receiver_sem failed");
        exit(EXIT_FAILURE);
    }

    // 開始計時：只測量實際的通訊操作
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mailbox_ptr->flag == MSG_PASSING) {
        // --- 訊息傳遞模式 ---
        // msgsnd 是一個系統呼叫，會將資料從使用者空間複製到核心空間的訊息佇列中
        if (msgsnd(mailbox_ptr->storage.msqid, &message, sizeof(message.msgText), 0) == -1) {
            perror("msgsnd failed");
            exit(EXIT_FAILURE);
        }
    } else { // SHARED_MEM
        // --- 共享記憶體模式 ---
        // strcpy 是一個函式庫呼叫，直接在使用者空間的映射記憶體中進行複製，速度非常快
        strcpy(shm_ptr->message_text, message.msgText);
    }

    // 停止計時
    clock_gettime(CLOCK_MONOTONIC, &end);
    total_send_time += (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;

    // 關鍵同步點 2: 通知 receiver 訊息已經準備好了。
    // sem_post 會增加 sender_sem 的值，喚醒正在等待它的 receiver。
    if (sem_post(&shm_ptr->sender_sem) == -1) {
        perror("sem_post sender_sem failed");
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
        perror("fopen failed");
        exit(EXIT_FAILURE);
    }

    mailbox_t mailbox;
    mailbox.flag = mechanism;
    message_t msg;
    msg.mType = 1; // 對於訊息佇列，類型必須 > 0
    
    // 將 shmid 和 msqid 初始化為 -1 是個好習慣，避免未初始化警告
    int shmid = -1, msqid = -1;

    // --- 初始化 IPC 資源 ---
    // 無論哪種模式，我們都需要一個共享記憶體來存放號誌
    shmid = shmget(SHM_KEY, sizeof(shm_payload_t), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    // 將共享記憶體區段附加到此行程的位址空間
    shm_ptr = (shm_payload_t *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    if (mechanism == MSG_PASSING) {
        printf("Message Passing\n");
        msqid = msgget(MSG_QUEUE_KEY, 0666 | IPC_CREAT);
        if (msqid == -1) {
            perror("msgget failed");
            exit(EXIT_FAILURE);
        }
        mailbox.storage.msqid = msqid;
    } else if (mechanism == SHARED_MEM) {
        printf("Shared Memory\n");
        mailbox.storage.shm_addr = (char *)shm_ptr;
    } else {
        fprintf(stderr, "無效的機制選項。\n");
        // 在離開前，清理已創建的資源
        shmdt(shm_ptr);
        shmctl(shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    // --- 初始化號誌 ---
    // 第二個參數 `1` 表示號誌將在行程間共享
    // sender_sem 初始為 0，因為一開始沒有訊息
    if (sem_init(&shm_ptr->sender_sem, 1, 0) == -1) {
        perror("sem_init sender_sem failed");
        exit(EXIT_FAILURE);
    }
    // receiver_sem 初始為 1，允許 sender 發送第一則訊息
    if (sem_init(&shm_ptr->receiver_sem, 1, 1) == -1) {
        perror("sem_init receiver_sem failed");
        exit(EXIT_FAILURE);
    }
    
    // --- 主迴圈：讀取檔案並發送訊息 ---
    char line_buffer[1024];
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
        strcpy(msg.msgText, line_buffer);
        printf("Sending message: %s", msg.msgText); // fgets 會讀取換行符，所以不用再加 \n
        send(msg, &mailbox);
    }
    fclose(fp);

    // --- 發送結束訊息 ---
    printf("End of input file! exit!\n");
    strcpy(msg.msgText, "EXIT\n");
    send(msg, &mailbox);

    // --- 輸出結果並清理 ---
    printf("Total time taken in sending msg: %.9f s\n", total_send_time);

    // 銷毀號誌
    sem_destroy(&shm_ptr->sender_sem);
    sem_destroy(&shm_ptr->receiver_sem);
    
    // 脫離並移除共享記憶體
    shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);

    // 如果是訊息傳遞模式，也要移除訊息佇列
    if (mechanism == MSG_PASSING) {
        msgctl(msqid, IPC_RMID, NULL);
    }

    return 0;
}