# Raspberry Pi - anagram_chat/I2C_LCD Driver

![image.png](attachment:9371f34f-66a6-404f-bad3-5f43c5e2ff59:image.png)

### 1. 개요

- 본 프로젝트는 **애너그램 퀴즈를 TCP/IP 기반의 채팅**을 통해 실시간으로 즐길 수 있도록 설계된 게임 시스템.
- **서버-클라이언트 구조**로 구성되어 있으며, 참가자가 정답을 맞출 경우 해당 내용은 **LCD(I2C)** 디바이스에 표시된다.
- 커널 모듈 방식으로 직접 작성한 `i2c_lcd_driver.c`를 통해 LCD를 제어

---

### 2. 시스템 아키텍처 / 흐름도

### 📊 시스템 흐름

- 클라이언트(명령어 입력) → TCP 소켓 통신 → server(퀴즈 출제 및 응답 처리) → 퀴즈 정답 여부 판단 → LCD 출력(Kernel Module)

```c
[서버 실행]
   |
   └─▶ 1. socket() 소켓 생성
   └─▶ 2. bind()로 포트 지정
   └─▶ 3. listen()으로 대기 시작
   └─▶ 4. select()로 클라이언트 연결 감시

[클라이언트 실행]
   |
   └─▶ 1. socket() 생성
   └─▶ 2. connect()로 서버 접속 요청

[서버]
   |
   └─▶ 5. accept()로 새 클라이언트 수락
   └─▶ 6. 클라이언트 소켓 관리 배열에 등록
   └─▶ 7. LCD 출력 및 브로드캐스트

[클라이언트]
   |
   └─▶ 3. 송신/수신 쓰레드 생성
         └─▶ send_msg(): 사용자 입력을 서버로 보냄
         └─▶ recv_msg(): 서버 메시지를 받아서 출력

[서버]
   |
   └─▶ 8. select()로 메시지 수신 감시
   └─▶ 9. 클라이언트 요청 종류에 따라 분기:
         ├─ "!quiz": 퀴즈 출제
         ├─ "!score": 점수판 요청
         ├─ "!rank" : 순위 요청
         ├─ "!exit" : 종료 요청
         └─ 일반 메시지: 채팅 브로드캐스트

   └─▶ 10. LCD에 메시지 출력

[클라이언트 종료]
   |
   └─▶ 쓰레드 종료 + 소켓 닫기

[서버]
   |
   └─▶ 클라이언트 연결 해제 감지
       └─ 소켓 닫고 배열에서 제거
       └─ LCD 인원 수 업데이트

```

---

### 3. 핵심 기능

1. 클라이언트 다중 접속 처리(멀티 스레드 기반)
2. 실시간 퀴즈 출제 및 정답 확인
3. 퀴즈 정답자 이름 → LCD 출력
4. 랭킹 확인 명령 → LCD에 출력
5. 커널 모듈로 I2C LCD 직접 제어 

 **3. 1 결과 화면**

![image.png](attachment:77e40e8e-6536-448b-87e4-c8fb7ee1fd92:image.png)

(사진 넣기)

- 자동 모드

![image.png](attachment:f86a67ad-ef83-420e-9982-6038fa34f1b3:image.png)

- LCD 출력 화면
- 서버 시작
    
    ![image.png](attachment:acbf2ee6-e836-417b-90b2-3b4543952c25:image.png)
    
    ![image.png](attachment:acbf2ee6-e836-417b-90b2-3b4543952c25:image.png)
    
- 현재 Player 수
    
    ![image.png](attachment:a9bd381a-adeb-40a5-be0d-4c1a6505a4d0:image.png)
    
- 문제 출력
    
    ![image.png](attachment:0ae358aa-dc39-49a2-9fbb-47bc8257d80e:image.png)
    
- 문제 맞춘 경우
    
    ![image.png](attachment:2672d99e-18a1-4941-92c8-6afe025df558:image.png)
    
- 현재랭킹 : !rank
    
    ![image.png](attachment:536c6789-317b-4016-8407-0c00d74b3dd2:image.png)
    
- 현재 우승 후보 : !score, 맞춘 문제 수
    
    ![image.png](attachment:d76f17d7-810a-4882-bde1-60bac3729d09:image.png)
    

- 시연 영상
    - 수동 모드 게임
    
    [1.mp4](attachment:f09bea3d-367c-406a-8851-d6709e4addb2:1.mp4)
    
    - 자동 모드 게임
    
    [5.mp4](attachment:140184e5-7525-479c-b6bc-4bd8e38f0f1b:5.mp4)
    

---

### 4. 기술 스택

| 분류 | 기술 |
| --- | --- |
| 언어 | C(커널 및 시스템 프로그래밍) |
| 통신 | TCP.IP 소켓(멀티 클라이언트) |
| 커널 모듈 | I2C 프로토콜, LCD1602 |
| 플랫폼 | Raspberry Pi(aarch64) |

---

### 5. 소스 코드

- server.c
    
    ```c
    // RaspberryPi server
    
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>
    #include <ctype.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <time.h>
    #include <fcntl.h> // open, O_WRONLY
    #include <errno.h> // errno
    
    #define MAX_CLIENT 10
    #define BUF_SIZE 1024
    #define LCD_DEVICE_PATH "/dev/i2c_lcd_display" // LCD 장치 파일 경로
    
    // 색상 매크로
    #define RESET   "\033[0m"
    #define RED     "\033[1;31m"
    #define GREEN   "\033[1;32m"
    #define YELLOW  "\033[1;33m"
    #define BLUE    "\033[1;34m"
    #define PURPLE  "\033[1;35m"
    #define CYAN    "\033[1;36m"
    #define LINE1 0x80
    #define LINE2 0xC0
    
    int client_socks[MAX_CLIENT];
    char client_names[MAX_CLIENT][30];
    int scores[MAX_CLIENT];
    int num_clients = 0;
    
    char current_answer[100] = "";
    int quiz_active = 0;
    time_t quiz_start_time = 0;
    
    char quiz_history[100][100];
    int quiz_history_count = 0;
    
    int lcd_fd = -1;
    
    void shuffle(const char* str, char* shuffled);
    int get_client_index(int sock);
    void broadcast(int sender, const char* msg);
    void send_to_lcd(const char* msg);
    
    void shuffle(const char* str, char* shuffled) {
        int len = strlen(str);
        strcpy(shuffled, str);
        for (int i = len - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            char tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }
    }
    
    int get_client_index(int sock) {
        for (int i = 0; i < num_clients; i++) {
            if (client_socks[i] == sock) return i;
        }
        return -1;
    }
    
    void broadcast(int sender, const char* msg) {
        for (int i = 0; i < num_clients; i++) {
            if (client_socks[i] != sender) {
                write(client_socks[i], msg, strlen(msg));
            }
        }
    }
    
    void send_to_lcd(const char* msg) {
        if (lcd_fd == -1) {
            fprintf(stderr, "LCD 장치가 열려 있지 않습니다. 메시지: '%s'\n", msg);
            return;
        }
    
        char lcd_output_buffer[33];
        memset(lcd_output_buffer, 0, sizeof(lcd_output_buffer));
    
        int msg_len = strlen(msg);
        int current_char_idx = 0;
        int buffer_idx = 0;
    
        while (current_char_idx < msg_len && buffer_idx < 16) {
            if (msg[current_char_idx] == '\n') {
                current_char_idx++;
                break;
            }
            lcd_output_buffer[buffer_idx++] = msg[current_char_idx++];
        }
    
        while (buffer_idx < 16) {
            lcd_output_buffer[buffer_idx++] = ' ';
        }
    
        lcd_output_buffer[buffer_idx++] = '\n';
    
        while (current_char_idx < msg_len && buffer_idx < 32) {
            if (msg[current_char_idx] == '\n') {
                current_char_idx++;
                break;
            }
            lcd_output_buffer[buffer_idx++] = msg[current_char_idx++];
        }
    
        while (buffer_idx < 32) {
            lcd_output_buffer[buffer_idx++] = ' ';
        }
    
        lcd_output_buffer[buffer_idx] = '\0';
    
        ssize_t bytes_written = write(lcd_fd, lcd_output_buffer, strlen(lcd_output_buffer));
        if (bytes_written == -1) {
            perror("LCD 장치에 쓰기 실패");
        }
    }
    
    int main() {
        srand(time(NULL));
        int serv_sock, clnt_sock;
        struct sockaddr_in serv_addr, clnt_addr;
        socklen_t clnt_addr_size;
        fd_set reads, cpy_reads;
        int fd_max;
        char buf[BUF_SIZE];
    
        lcd_fd = open(LCD_DEVICE_PATH, O_WRONLY);
        if (lcd_fd == -1) {
            fprintf(stderr, "경고: '%s'를 열 수 없습니다. LCD 출력이 비활성화됩니다. (%s)\n", LCD_DEVICE_PATH, strerror(errno));
        }
        else {
            printf("I2C LCD 장치 '%s' 열림.\n", LCD_DEVICE_PATH);
            send_to_lcd("Server Started!\nPort:8888");
        }
    
        serv_sock = socket(PF_INET, SOCK_STREAM, 0);
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(8888);
    
        bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        listen(serv_sock, 5);
    
        FD_ZERO(&reads);
        FD_SET(serv_sock, &reads);
        fd_max = serv_sock;
    
        printf("서버 시작 (포트 8888)\n");
    
        while (1) {
            cpy_reads = reads;
            if (select(fd_max + 1, &cpy_reads, 0, 0, NULL) == -1) break;
    
            for (int i = 0; i <= fd_max; i++) {
                if (FD_ISSET(i, &cpy_reads)) {
                    if (i == serv_sock) {
                        clnt_addr_size = sizeof(clnt_addr);
                        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
    
                        if (num_clients >= MAX_CLIENT) {
                            char* msg = "서버가 꽉 찼습니다.\n";
                            write(clnt_sock, msg, strlen(msg));
                            close(clnt_sock);
                            continue;
                        }
    
                        FD_SET(clnt_sock, &reads);
                        if (fd_max < clnt_sock) fd_max = clnt_sock;
                        client_socks[num_clients] = clnt_sock;
    
                        write(clnt_sock, "닉네임을 입력하세요: ", 30);
                        int name_len = read(clnt_sock, client_names[num_clients], 30);
                        client_names[num_clients][strcspn(client_names[num_clients], "\n")] = 0;
                        scores[num_clients] = 0;
                        num_clients++;
    
                        char join_msg[100];
                        sprintf(join_msg, "👤 %s 님이 입장하였습니다.\n", client_names[num_clients - 1]);
                        broadcast(-1, join_msg);
                        printf("연결됨: %s\n", client_names[num_clients - 1]);
    
                        char lcd_player_msg[33];
                        snprintf(lcd_player_msg, sizeof(lcd_player_msg), "Players: %d", num_clients);
                        send_to_lcd(lcd_player_msg);
    
                    }
                    else {
                        int str_len = read(i, buf, BUF_SIZE - 1);
                        if (str_len <= 0) {
                            int idx = get_client_index(i);
                            if (idx == -1) continue;
    
                            printf("연결 종료: %s\n", client_names[idx]);
    
                            char leave_msg[100];
                            sprintf(leave_msg, "👤 %s 님이 나갔습니다.\n", client_names[idx]);
                            broadcast(-1, leave_msg);
    
                            close(i);
                            FD_CLR(i, &reads);
                            for (int k = idx; k < num_clients - 1; k++) {
                                client_socks[k] = client_socks[k + 1];
                                strcpy(client_names[k], client_names[k + 1]);
                                scores[k] = scores[k + 1];
                            }
                            num_clients--;
    
                            char lcd_player_msg[33];
                            snprintf(lcd_player_msg, sizeof(lcd_player_msg), "Players: %d", num_clients);
                            send_to_lcd(lcd_player_msg);
    
                        }
                        else {
                            buf[str_len] = '\0';
                            buf[strcspn(buf, "\n")] = '\0';
                            int idx = get_client_index(i);
                            if (idx == -1) continue;
    
                            if (strcmp(buf, "!exit") == 0) {
                                write(i, "종료합니다.\n", 20);
                                close(i);
                                FD_CLR(i, &reads);
    
                                char msg[100];
                                sprintf(msg, "👤 %s 님이 나갔습니다.\n", client_names[idx]);
                                broadcast(-1, msg);
    
                                for (int k = idx; k < num_clients - 1; k++) {
                                    client_socks[k] = client_socks[k + 1];
                                    strcpy(client_names[k], client_names[k + 1]);
                                    scores[k] = scores[k + 1];
                                }
                                num_clients--;
    
                                char lcd_player_msg[33];
                                snprintf(lcd_player_msg, sizeof(lcd_player_msg), "Players: %d", num_clients);
                                send_to_lcd(lcd_player_msg);
                            }
                            else if (strncmp(buf, "!quiz ", 6) == 0) {
                                if (quiz_active) {
                                    write(i, " 이미 퀴즈가 진행 중입니다.\n", 60);
                                }
                                else {
                                    char* new_word = buf + 6;
                                    if (strlen(new_word) < 2 || strlen(new_word) >= sizeof(current_answer)) {
                                        write(i, " 퀴즈 단어는 2글자 이상, 99글자 이하로 입력해주세요.\n", 80);
                                        continue;
                                    }
    
                                    int duplicate = 0;
                                    for (int h = 0; h < quiz_history_count; h++) {
                                        if (strcmp(new_word, quiz_history[h]) == 0) {
                                            duplicate = 1;
                                            break;
                                        }
                                    }
                                    if (duplicate) {
                                        write(i, " 이미 출제된 단어입니다.\n", 60);
                                    }
                                    else {
                                        strcpy(current_answer, new_word);
                                        strcpy(quiz_history[quiz_history_count++], current_answer);
                                        char quiz_shuffled[100];
                                        shuffle(current_answer, quiz_shuffled);
                                        char quiz_msg[150];
                                        snprintf(quiz_msg, sizeof(quiz_msg), "🧠 [퀴즈] %s 님이 문제 출제: %s\n", client_names[idx], quiz_shuffled);
                                        broadcast(-1, quiz_msg);
                                        quiz_active = 1;
                                        quiz_start_time = time(NULL);
    
                                        char lcd_quiz_msg[33];
                                        snprintf(lcd_quiz_msg, sizeof(lcd_quiz_msg), "QUIZ:%.16s\n%s", new_word, quiz_shuffled);
                                        send_to_lcd(lcd_quiz_msg);
                                    }
                                }
                            }
                            else if (strcmp(buf, "!score") == 0) {
                                char score_msg[512] = "[점수판]\n";
    
                                int top_score = -1;
                                char top_scorer_name[30] = "No players";
                                if (num_clients > 0) {
                                    top_score = scores[0];
                                    strcpy(top_scorer_name, client_names[0]);
                                    for (int s = 1; s < num_clients; s++) {
                                        if (scores[s] > top_score) {
                                            top_score = scores[s];
                                            strcpy(top_scorer_name, client_names[s]);
                                        }
                                    }
                                }
    
                                for (int s = 0; s < num_clients; s++) {
                                    char line[100];
                                    sprintf(line, "%s: %d점\n", client_names[s], scores[s]);
                                    strcat(score_msg, line);
                                }
                                write(i, score_msg, strlen(score_msg));
    
                                char temp_lcd_score[33];
                                if (num_clients > 0) {
                                    snprintf(temp_lcd_score, sizeof(temp_lcd_score), "Top Score!\n%.16s: %d", top_scorer_name, top_score);
                                }
                                else {
                                    snprintf(temp_lcd_score, sizeof(temp_lcd_score), "Scoreboard\nNo Players");
                                }
                                send_to_lcd(temp_lcd_score);
                            }
                            else if (strcmp(buf, "!rank") == 0) {
                                int temp_score[MAX_CLIENT];
                                char temp_name[MAX_CLIENT][30];
                                for (int t = 0; t < num_clients; t++) {
                                    temp_score[t] = scores[t];
                                    strcpy(temp_name[t], client_names[t]);
                                }
    
                                for (int x = 0; x < num_clients - 1; x++) {
                                    for (int y = x + 1; y < num_clients; y++) {
                                        if (temp_score[x] < temp_score[y]) {
                                            int ts = temp_score[x];
                                            temp_score[x] = temp_score[y];
                                            temp_score[y] = ts;
                                            char tn[30];
                                            strcpy(tn, temp_name[x]);
                                            strcpy(temp_name[x], temp_name[y]);
                                            strcpy(temp_name[y], tn);
                                        }
                                    }
                                }
    
                                char rank_msg[512] = "[🏆 순위표]\n";
                                for (int r = 0; r < num_clients; r++) {
                                    char line[100];
                                    sprintf(line, "%d위: %s (%d점)\n", r + 1, temp_name[r], temp_score[r]);
                                    strcat(rank_msg, line);
                                }
                                write(i, rank_msg, strlen(rank_msg));
    
                                char lcd_rank_msg[33];
                                if (num_clients >= 2) {
                                    snprintf(lcd_rank_msg, sizeof(lcd_rank_msg), "1st:%.8s %d\n2nd:%.8s %d",
                                        temp_name[0], temp_score[0], temp_name[1], temp_score[1]);
                                }
                                else if (num_clients == 1) {
                                    snprintf(lcd_rank_msg, sizeof(lcd_rank_msg), "1st:%.8s %d\n", temp_name[0], temp_score[0]);
                                }
                                else {
                                    snprintf(lcd_rank_msg, sizeof(lcd_rank_msg), "Rankings\nNo Players");
                                }
                                send_to_lcd(lcd_rank_msg);
                            }
                            else if (quiz_active) {
                                time_t now = time(NULL);
                                if (difftime(now, quiz_start_time) > 15.0) {
                                    write(i, "⏰ 제한 시간이 초과되었습니다. 퀴즈 종료.\n", 60);
    
                                    char timeout_broadcast_msg[150];
                                    snprintf(timeout_broadcast_msg, sizeof(timeout_broadcast_msg),
                                        "⏰ 퀴즈 제한 시간 초과! 정답은: %s%s%s\n",
                                        YELLOW, current_answer, RESET);
                                    broadcast(-1, timeout_broadcast_msg);
    
                                    char lcd_timeout_msg[33];
                                    snprintf(lcd_timeout_msg, sizeof(lcd_timeout_msg), "Time's Up!\nAns: %.16s", current_answer);
                                    send_to_lcd(lcd_timeout_msg);
    
                                    quiz_active = 0;
                                    current_answer[0] = '\0';
                                }
                                else if (strcmp(buf, current_answer) == 0) {
                                    scores[idx]++;
                                    char win_msg[150];
                                    snprintf(win_msg, sizeof(win_msg), "🎉 정답! %s 님이 %s을(를) 맞췄습니다! (+1점)\n", client_names[idx], current_answer);
                                    broadcast(-1, win_msg);
                                    quiz_active = 0;
    
                                    char lcd_win_msg[33];
                                    snprintf(lcd_win_msg, sizeof(lcd_win_msg), "WINNER:%.16s\nAns:%.16s", client_names[idx], current_answer);
                                    send_to_lcd(lcd_win_msg);
                                    current_answer[0] = '\0';
                                }
                                else {
                                    char wrong_answer_msg[100];
                                    snprintf(wrong_answer_msg, sizeof(wrong_answer_msg), RED "❌ 틀렸습니다. 다시 시도하세요.\n" RESET);
                                    write(i, wrong_answer_msg, strlen(wrong_answer_msg));
    
                                    char broadcast_wrong_msg[100];
                                    snprintf(broadcast_wrong_msg, sizeof(broadcast_wrong_msg), "%s%s 님이 오답을 시도했습니다.\n%s", CYAN, client_names[idx], RESET);
                                    broadcast(i, broadcast_wrong_msg);
                                }
                            }
                            else {
                                char chat_msg[BUF_SIZE + 50];
                                sprintf(chat_msg, "%s: %s\n", client_names[idx], buf);
                                broadcast(i, chat_msg);
                            }
                        }
                    }
                }
            }
        }
    
        if (lcd_fd != -1) {
            close(lcd_fd);
            printf("I2C LCD 장치 '%s' 닫힘.\n", LCD_DEVICE_PATH);
        }
        close(serv_sock);
        return 0;
    }
    
    ~                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         ~                                                               
    ```
    
    - 클라이언트 다중 접속 처리 (thread
- client.c
    
    ```c
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
    ```
    
- i2c_lcd_driver.c
    
    ```c
    // i2c_lcd_driver.c
    #include <linux/module.h>
    #include <linux/init.h>
    #include <linux/kernel.h>
    #include <linux/i2c.h>
    #include <linux/delay.h>
    #include <linux/workqueue.h>
    #include <linux/string.h>
    #include <linux/fs.h>
    #include <linux/uaccess.h>
    #include <linux/cdev.h>
    
    // --- LCD 설정 ---
    #define LCD_I2C_ADDR 0x27
    #define MAX_MSG_LEN  32
    #define LCD_MAJOR    240
    #define DEVICE_NAME  "i2c_lcd_display"
    
    // PCF8574 핀 매핑
    #define RS_BIT       (1 << 0)
    #define RW_BIT       (1 << 1)
    #define EN_BIT       (1 << 2)
    #define BL_BIT       (1 << 3)
    
    // LCD 명령어
    #define LCD_CMD_CLEARDISPLAY   0x01
    #define LCD_CMD_RETURNHOME     0x02
    #define LCD_CMD_ENTRYMODESET   0x06
    #define LCD_CMD_DISPLAYCONTROL 0x0C
    #define LCD_CMD_FUNCTIONSET    0x28
    
    static struct i2c_client *lcd_i2c_client;
    static struct workqueue_struct *lcd_wq;
    static struct delayed_work lcd_work;
    static char current_lcd_message[MAX_MSG_LEN + 1];
    static struct cdev lcd_cdev;
    static dev_t lcd_dev_num;
    
    static int i2c_lcd_write_byte(u8 data, u8 mode)
    {
        int ret;
        u8 tx = data | mode | BL_BIT;
    
        ret = i2c_smbus_write_byte(lcd_i2c_client, tx | EN_BIT);
        if (ret < 0) {
            pr_err("LCD write EN-high failed: %d\n", ret);
            return ret;
        }
        udelay(5);
    
        ret = i2c_smbus_write_byte(lcd_i2c_client, tx & ~EN_BIT);
        if (ret < 0) {
            pr_err("LCD write EN-low failed: %d\n", ret);
            return ret;
        }
        udelay(200);
        return 0;
    }
    
    static void lcd_send_nibbles(u8 val, u8 mode)
    {
        i2c_lcd_write_byte(val & 0xF0, mode);
        i2c_lcd_write_byte((val << 4) & 0xF0, mode);
    }
    
    static void lcd_send_cmd(u8 cmd)
    {
        lcd_send_nibbles(cmd, 0);
        if (cmd == LCD_CMD_CLEARDISPLAY || cmd == LCD_CMD_RETURNHOME)
            mdelay(3);
    }
    
    static void lcd_send_data(u8 data)
    {
        lcd_send_nibbles(data, RS_BIT);
    }
    
    static void lcd_set_cursor(u8 col, u8 row)
    {
        u8 addr = (row == 0 ? 0x80 : 0xC0) + col;
        lcd_send_cmd(addr);
    }
    
    static void lcd_print_string(const char *s)
    {
        while (*s) {
            lcd_send_data(*s++);
        }
    }
    
    static void lcd_display_user_message_fn(struct work_struct *work)
    {
        size_t len = strlen(current_lcd_message);
        size_t i;
    
        pr_info("I2C LCD: Displaying user message: '%s'\n", current_lcd_message);
    
        lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
        mdelay(5);
        lcd_send_cmd(LCD_CMD_RETURNHOME);
        mdelay(5);
    
        for (i = 0; i < len; ++i) {
            if (i == 16) {
                lcd_set_cursor(0, 1);
            }
            if (i >= MAX_MSG_LEN) {
                break;
            }
            lcd_send_data(current_lcd_message[i]);
        }
    }
    
    static void lcd_init_sequence(void)
    {
        int i;
        mdelay(100);
    
        for (i = 0; i < 3; ++i) {
            i2c_lcd_write_byte(0x30, 0);
            udelay(6000);
        }
        i2c_lcd_write_byte(0x20, 0);
        udelay(300);
    
        lcd_send_cmd(LCD_CMD_FUNCTIONSET);
        lcd_send_cmd(LCD_CMD_DISPLAYCONTROL);
        lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
        mdelay(3);
        lcd_send_cmd(LCD_CMD_ENTRYMODESET);
    }
    
    // --- 문자 장치 파일 오퍼레이션 ---
    static int lcd_open(struct inode *inode, struct file *file)
    {
        pr_info("I2C LCD: Device opened.\n");
        return 0;
    }
    
    static int lcd_release(struct inode *inode, struct file *file)
    {
        pr_info("I2C LCD: Device closed.\n");
        return 0;
    }
    
    static ssize_t lcd_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
    {
        if (count > MAX_MSG_LEN) {
            pr_warn("I2C LCD: Message too long, truncating to %d characters.\n", MAX_MSG_LEN);
            count = MAX_MSG_LEN;
        }
    
        if (copy_from_user(current_lcd_message, buf, count)) {
            pr_err("I2C LCD: Failed to copy data from user space.\n");
            return -EFAULT;
        }
        current_lcd_message[count] = '\0';
    
        cancel_delayed_work_sync(&lcd_work);
        INIT_DELAYED_WORK(&lcd_work, lcd_display_user_message_fn);
        queue_delayed_work(lcd_wq, &lcd_work, msecs_to_jiffies(100));
    
        pr_info("I2C LCD: Received message from user: '%s'\n", current_lcd_message);
    
        return count;
    }
    
    static const struct file_operations lcd_fops = {
        .owner   = THIS_MODULE,
        .open    = lcd_open,
        .release = lcd_release,
        .write   = lcd_write,
    };
    
    // --- I2C 드라이버 부분 ---
    static int lcd_probe(struct i2c_client *client)
    {
        int ret;
        lcd_i2c_client = client;
        pr_info("I2C LCD: Probing LCD at address 0x%x on I2C bus %d\n",
                client->addr, i2c_adapter_id(client->adapter));
    
        if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
            pr_err("I2C LCD: I2C SMBus byte data functionality not supported.\n");
            return -EIO;
        }
    
        lcd_init_sequence();
    
        lcd_wq = create_singlethread_workqueue("lcd_wq");
        if (!lcd_wq) {
            pr_err("I2C LCD: Failed to create workqueue.\n");
            return -ENOMEM;
        }
    
        strncpy(current_lcd_message, "Hello from Kernel!", MAX_MSG_LEN);
        current_lcd_message[MAX_MSG_LEN] = '\0';
        INIT_DELAYED_WORK(&lcd_work, lcd_display_user_message_fn);
        queue_delayed_work(lcd_wq, &lcd_work, msecs_to_jiffies(500));
    
        ret = alloc_chrdev_region(&lcd_dev_num, 0, 1, DEVICE_NAME);
        if (ret < 0) {
            pr_err("I2C LCD: Failed to allocate char device region.\n");
            destroy_workqueue(lcd_wq);
            return ret;
        }
    
        cdev_init(&lcd_cdev, &lcd_fops);
        lcd_cdev.owner = THIS_MODULE;
        ret = cdev_add(&lcd_cdev, lcd_dev_num, 1);
        if (ret < 0) {
            pr_err("I2C LCD: Failed to add char device.\n");
            unregister_chrdev_region(lcd_dev_num, 1);
            destroy_workqueue(lcd_wq);
            return ret;
        }
    
        pr_info("I2C LCD: Driver loaded and device /dev/%s created (Major: %d, Minor: %d).\n",
                DEVICE_NAME, MAJOR(lcd_dev_num), MINOR(lcd_dev_num));
    
        return 0;
    }
    
    static void lcd_remove(struct i2c_client *client)
    {
        cdev_del(&lcd_cdev);
        unregister_chrdev_region(lcd_dev_num, 1);
    
        cancel_delayed_work_sync(&lcd_work);
        if (lcd_wq)
            destroy_workqueue(lcd_wq);
    
        lcd_send_cmd(LCD_CMD_CLEARDISPLAY);
        mdelay(3);
        i2c_smbus_write_byte(client, 0x00);
        pr_info("I2C LCD: Driver unloaded and device /dev/%s removed.\n", DEVICE_NAME);
    }
    
    static const struct i2c_device_id lcd_id[] = {
        { "i2c_lcd", 0 },
        { }
    };
    MODULE_DEVICE_TABLE(i2c, lcd_id);
    
    static struct i2c_driver lcd_driver = {
        .driver = {
            .name = "i2c_lcd",
            .owner = THIS_MODULE,
        },
        .probe  = lcd_probe,
        .remove = lcd_remove,
        .id_table = lcd_id,
    };
    
    static int __init i2c_lcd_module_init(void)
    {
        pr_info("I2C LCD: Initializing I2C LCD driver module.\n");
        return i2c_add_driver(&lcd_driver);
    }
    
    static void __exit i2c_lcd_module_exit(void)
    {
        i2c_del_driver(&lcd_driver);
        pr_info("I2C LCD: Exiting I2C LCD driver module.\n");
    }
    
    module_init(i2c_lcd_module_init);
    module_exit(i2c_lcd_module_exit);
    
    MODULE_AUTHOR("Your Name");
    MODULE_DESCRIPTION("I2C LCD driver with user space interface");
    MODULE_LICENSE("GPL");
    
    ```
    
- Makefile
    
    ```c
    obj-m := i2c_lcd_driver.o
    KDIR := $(HOME)/project/linux
    PWD  := $(shell pwd)
    
    all:
        make -C $(KDIR) M=$(PWD) modules
    
    clean:
        make -C $(KDIR) M=$(PWD) clean
    ```
    
- 서버 실행 명령어
    
    ```c
    echo i2c_lcd 0x27 | sudo tee /sys/bus/i2c/devices/i2c-1/new_device
    dmesg | grep i2c_lcd
    echo "Hello LCD" > /dev/i2c_lcd_display
    
    <RaspberryPi>
    gcc server4.c -o server4
    ./server4 -> /dev/i2c_icd_display 열린거 확인해야함.
    
    <ubuntu>
    gcc client -o client
    ./client 10.10.16.134 8888
    
    ```
    

---

### 6. 고찰 및 개선 방향

- LCD 커서 이동 문제
    - `\n` 문자로 줄바꿈을 처리하려고 했으나, LCD는 직접 커서 주소를 지정해야 함
    - `\n` 입력 시 이상한 아스키 코드(0x0A)가 출력됨
    - **개선**: 줄별 시작 주소를 미리 정의하고, 줄바꿈 시 커서 주소를 직접 설정하는 함수로 처리
- 자동 문제 출제 코드 통합 문제
    - 수동/자동 모드로 코드 관리했어야하는데 하나로 합치지 못함, 함수 단위로 통합, 재사용 가능하도록 구조화 보완 필요
