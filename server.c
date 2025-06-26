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
