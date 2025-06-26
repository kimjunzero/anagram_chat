#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

void *recv_msg(void *arg);
void *send_msg(void *arg);
void error_handling(const char *msg);

// 색상 매크로
#define RESET   "\033[0m"
#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[1;34m"
#define PURPLE  "\033[1;35m"
#define CYAN    "\033[1;36m"

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread, rcv_thread;
    void *thread_return;

    if (argc != 3) {
        printf("사용법: %s <서버IP> <포트>\n", argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() 오류");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() 오류");

    // 수신/송신 스레드 생성
    pthread_create(&rcv_thread, NULL, recv_msg, (void *)&sock);
    pthread_create(&snd_thread, NULL, send_msg, (void *)&sock);

    pthread_join(snd_thread, &thread_return);
    pthread_cancel(rcv_thread);
    close(sock);
    return 0;
}

void *send_msg(void *arg) {
    int sock = *((int *)arg);
    char msg[BUF_SIZE];

    while (1) {
        printf(GREEN "[Me] > " RESET);
        fflush(stdout);

        fgets(msg, BUF_SIZE, stdin);

        if (!strcmp(msg, "!exit\n")) {
            write(sock, msg, strlen(msg));
            break;
        }

        write(sock, msg, strlen(msg));
    }

    return NULL;
}

void *recv_msg(void *arg) {
    int sock = *((int *)arg);
    char msg[BUF_SIZE];
    int str_len;

    while (1) {
        str_len = read(sock, msg, BUF_SIZE - 1);
        if (str_len <= 0) return NULL;
        msg[str_len] = 0;

        if (strstr(msg, "입장하였습니다")) {
            printf(BLUE "%s" RESET, msg);
        }
        else if (strstr(msg, "나갔습니다")) {
            printf(PURPLE "%s" RESET, msg);
        }
        else if (strstr(msg, "정답!") || strstr(msg, "🎉")) {
            printf(YELLOW "%s" RESET, msg);
        }
        else if (strstr(msg, "[퀴즈]")) {
            printf(CYAN "%s" RESET, msg);
        }
        else if (strstr(msg, "[점수판]") || strstr(msg, "[🏆")) {
            printf(YELLOW "%s" RESET, msg);
        }
        else {
            printf("%s", msg);
        }

        printf(GREEN "[Me] > " RESET);
        fflush(stdout);
    }

    return NULL;
}

void error_handling(const char *msg) {
    fputs(RED, stderr);
    fputs(msg, stderr);
    fputs(RESET "\n", stderr);
    exit(1);
}
