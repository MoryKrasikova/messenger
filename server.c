#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#define _GNU_SOURCE
#include <string.h>

int server_fd;
#define PORT 8888
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

void trim(char *str) {
    char *start = str;
    char *end;
    
    while(*start == ' ' || *start == '\n' || *start == '\r') start++;
    if(*start == '\0') { str[0] = '\0'; return; }
    
    end = start + strlen(start) - 1;
    while(end > start && (*end == ' ' || *end == '\n' || *end == '\r')) end--;
    
    int len = end - start + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

//структура клиента
typedef struct {
    int socket;
    char name[32];
    int active;
    int in_common_chat;
    char private_chat_with[32];
} Client;
//массив клиентов
Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

//список всех пользователей, какие заходили в чат
typedef struct{
    char name[32];
    int exists;
} RegisteredUser;
RegisteredUser registered_users[100];
int registered_count = 0;
pthread_mutex_t registered_mutex = PTHREAD_MUTEX_INITIALIZER;


//уведомления в личных чатах
void notify_private_chat(const char *client_name, const char *partner, const char *status){
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[Система]: %s %s личный чат", client_name, status);

    pthread_mutex_lock(&client_mutex);
    for(int i = 0; i < client_count; i++){
        if(clients[i].active && strcmp(clients[i].name, partner) == 0 && strcmp(clients[i].private_chat_with, client_name) == 0){
            send(clients[i].socket, msg, strlen(msg), 0);
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

//создание списка пользователей и добавление новых
void load_registered_users(){
    FILE *f = fopen("users.txt", "r");
    if(!f) return;
    registered_count = 0;
    while(fscanf(f, "%31s \n", registered_users[registered_count].name) == 1){
        registered_users[registered_count].exists = 1;
        registered_count ++;
    }
    fclose(f);
}

void save_registered_users(){
    FILE *f = fopen("users.txt", "w");
    if (!f) return;
    for(int i = 0; i < registered_count; i++){
        fprintf(f, "%s\n", registered_users[i].name);
    }
    fclose(f);
}

int(user_exists(const char *name)){
    for( int i = 0; i < registered_count; i++){
        if(strcmp(registered_users[i].name, name) == 0){
            return 1;
        }
    }
    return 0;
}

void add_registered_user(const char *name){
    pthread_mutex_lock(&registered_mutex);

    if(user_exists(name)) return;
    if(registered_count < 100){
        strcpy(registered_users[registered_count].name, name);
        registered_users[registered_count].exists = 1;
        registered_count++;
        save_registered_users();
    }

    pthread_mutex_unlock(&registered_mutex);
}

//функция обработчик для завершения сервера
void handle_sigint(int sig){
    printf("\nВыключение сервера\n");
    log_server("Server Stop");
    close(server_fd);
    exit(0);
}

//логи сервера 
void log_server(const char *event){
    FILE *f = fopen("server.log", "a");
    if (!f) return;

    time_t t = time(NULL);
    char *time_str = ctime(&t);
    time_str[strlen(time_str) - 1] = '\0';

    fprintf(f, "[%s] %s\n", time_str, event);
    fclose(f);
}

//сохранение истории
void save_message_to_history(const char *from , const char *to, const char *message, int is_group){
    char filename[128];
    if(is_group){
        snprintf(filename, sizeof(filename), "chat_broadcast.log");
    }
    else{
        char name1[32], name2[32];
        strcpy(name1, from);
        strcpy(name2, to);
        if (strcmp(name1, name2) > 0)
            snprintf(filename, sizeof(filename), "chat_%s_%s.log", name2, name1);
        else
            snprintf(filename, sizeof(filename), "chat_%s_%s.log", name1, name2);
    }

    FILE *f = fopen(filename, "a");
    if (!f) return;
    fprintf(f, "[%s]: %s\n", from, message);
    fclose(f);
}

//отправка истории клиенту
void send_chat_history(int client_sock, const char *current_user, const char *chat_name, int is_group) {
    char filename[128];
    if (is_group) {
        snprintf(filename, sizeof(filename), "chat_broadcast.log");
    } 
    else {
        char name1[32], name2[32];
        strcpy(name1, current_user);
        strcpy(name2, chat_name);
        if (strcmp(name1, name2) > 0)
            snprintf(filename, sizeof(filename), "chat_%s_%s.log", name2, name1);
        else
            snprintf(filename, sizeof(filename), "chat_%s_%s.log", name1, name2);
    }

    FILE *f = fopen(filename, "r");
    if (!f) return;

    send(client_sock, "HISTORY_START", 13, 0);
    char line[BUFFER_SIZE];
    while(fgets(line, sizeof(line), f)){
        line[strcspn(line, "\n")] = '\0';
        send(client_sock, line, strlen(line), 0);
        send(client_sock, "\n", 1, 0);
    }
    fclose(f);
    send(client_sock, "HISTORY_END", 11, 0);
}

//сообщение для всех
void broadcast_messenge(const char *sender_name, const char *message, int sender_socket){
    if(strcmp(sender_name, "Система") != 0){
        save_message_to_history(sender_name, "ALL", message, 1);
    }
    char full_message[BUFFER_SIZE];
    snprintf(full_message, sizeof(full_message), "[%s]: %s", sender_name, message);

    pthread_mutex_lock(&client_mutex);
    for(int i = 0; i<client_count; i++)
    {
        if(clients[i].active && clients[i].socket != sender_socket){
            send(clients[i].socket, full_message, strlen(full_message), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

//личные сообщения 
void send_private_msg(const char *recipient_name, const char *sender_name, const char *message, 
int sender_sock){
    char full_message[BUFFER_SIZE];
    snprintf(full_message, sizeof(full_message), "[ЛС от %s]: %s", sender_name, message);

    save_message_to_history(sender_name, recipient_name, message, 0);

    pthread_mutex_lock(&client_mutex);
    for(int i = 0; i<client_count; i++){
        if(clients[i].active && strcasecmp(clients[i].name, recipient_name) == 0){
            send(clients[i].socket, full_message, strlen(full_message), 0);
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

//обработка клиента
void *handle_client(void *arg){
    int client_sock = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    int bytes;
    char client_name[32];
    int my_connection_id;

//получаем имя
    bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if(bytes <= 0){
        close(client_sock);
        return NULL;
    }
    buffer[bytes] = '\0';
    trim(buffer);
    strcpy(client_name, buffer);

//добавляем в массив
    pthread_mutex_lock(&client_mutex);

    int found = -1;
    for(int i = 0; i < client_count; i++){
        if(clients[i].active && strcmp(clients[i].name, client_name) == 0){
            found = i;
            break;
        }
    }

    if(found != -1){
        close(clients[found].socket);
        clients[found].socket = client_sock;
        clients[found].active = 1;
        clients[found].in_common_chat = 0;
        clients[found].private_chat_with[0] = '\0';
    }
    else{
        int slot = -1;
        for(int i = 0; i < client_count; i++){
            if(!clients[i].active){
                slot = i;
                break;
            }
        }

        if(slot == -1){
            if(client_count >= MAX_CLIENTS){
                pthread_mutex_unlock(&client_mutex);
                close(client_sock);
                return NULL;
            }
            slot = client_count;
            client_count++;
        }
        clients[slot].socket = client_sock;
        strcpy(clients[slot].name, client_name);
        clients[slot].active = 1;
        clients[slot].in_common_chat = 0;
        clients[slot].private_chat_with[0] = '\0';
    }

    pthread_mutex_unlock(&client_mutex);

    printf("[%s] Подключился/лась\n", client_name);

//добавляем сообщение о подключение в логи
    char log_buf[64];
    snprintf(log_buf, sizeof(log_buf), "User Connect: %s", client_name);
    log_server(log_buf);

//добавляем в список пользователей
    add_registered_user(client_name);

//обработка сообщений от пользователя
    while(1){
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) break;
        buffer[bytes] = '\0';

//проверка существует ли пользователь
        if(strncmp(buffer, "CHECK_USER ", 11) == 0){
            char check_name[32];
            sscanf(buffer + 11, "%31s", check_name);
            trim(check_name);
            if(user_exists(check_name)){
                send(client_sock, "USER_EXISTS", 11, 0);
            }
            else send(client_sock, "USER_NOT_EXISTS", 15, 0);
            continue;
        }

//получение истории
        if(strncmp(buffer, "GET_HISTORY ", 12) == 0){
            char chat_name[64];
            sscanf(buffer + 12, "%63s", chat_name);
            int is_group = (strcmp(chat_name, "BROADCAST") == 0);
            send_chat_history(client_sock, client_name, chat_name, is_group);
            continue;
        }

//уведомления о входах в чаты
        if(strcmp(buffer, "ENTER_COMMON") == 0){
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    clients[i].in_common_chat = 1;
                    break;
                }
            }
            char enter_msg[BUFFER_SIZE];
            snprintf(enter_msg, sizeof(enter_msg), " %s вошёл/а в чат", client_name);
            broadcast_messenge("Система", enter_msg, client_sock);
            continue;
        }

        if(strncmp(buffer, "ENTER_PRIVATE ", 14) == 0){
            char partner[32];
            sscanf(buffer + 14, "%31s", partner);
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    strcpy(clients[i].private_chat_with, partner);
                    break;
                }
            }
            for(int i = 0; i < client_count; i++){
                if(strcmp(clients[i].name, partner) == 0 && clients[i].active){
                    notify_private_chat(client_name, partner, "вошёл/а в");
                    break;
                }
            }
            continue;
        }

//уведомления о выходах из чатов
        if(strcmp(buffer, "LEAVE_COMMON") == 0){
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    clients[i].in_common_chat = 0;
                    break;
                }
            }
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, sizeof(leave_msg), " %s вышел/а из чата", client_name);
            broadcast_messenge("Система", leave_msg, client_sock);
            continue;
        }

        if(strcmp(buffer, "LEAVE_PRIVATE") == 0){
            char partner[32] = "";
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    strcpy(partner, clients[i].private_chat_with);
                    clients[i].private_chat_with[0] = '\0';
                    break;
                }
            }
            
            if(strlen(partner) > 0){
                notify_private_chat(client_name, partner, "вышел/а из");
            }
            continue;
        }

        if(strcmp(buffer, "/exit") == 0) break;
//личное сообщение
        if(buffer[0] == '@'){
            char recipient[32];
            char msg[BUFFER_SIZE];

            if(sscanf(buffer, "@%31s %[^\n]", recipient, msg) == 2){
                printf("[%s] -> [%s]: %s\n", client_name, recipient, msg);
                send_private_msg(recipient, client_name, msg, client_sock);
            }
            else{
                char err_msg[] = "Неверный формат. Используйте: @имя_получателя сообщение\n";
                send(client_sock, err_msg, strlen(err_msg), 0);
            }
        }
//для всех
        else {
            printf("[%s]: %s\n", client_name, buffer);
            broadcast_messenge(client_name, buffer, client_sock);
        }
    }

//удаляем клиента при выходе
    pthread_mutex_lock(&client_mutex);
    for(int i = 0; i<client_count; i++){
        if(clients[i].socket == client_sock){
            clients[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);

//добавление сообщения о выходе в логи
    char leave_msg[BUFFER_SIZE];

    snprintf(log_buf, sizeof(log_buf), "User Disconnect: %s", client_name);
    log_server(log_buf);

    close(client_sock);
    return NULL;
}

int main(){
    setlocale(LC_ALL, "ru_RU.UTF-8");
    int *client_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    pthread_t thread;

    signal(SIGINT, handle_sigint);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);
    log_server("Server Start");

    printf("Сервер слушает порт %d\n", PORT);

    load_registered_users();

    while(1){
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }

    return 0;
}