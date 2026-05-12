#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <locale.h>
#include <time.h>
#include <signal.h>

#define _GNU_SOURCE
#include <string.h>

int server_fd;
#define PORT 8888
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
//структура клиента
typedef struct {
    int socket;
    char name[32];
    int active;
    int in_common_chat;
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

//сообщение для всех
void broadcast_messenge(const char *sender_name, const char *message, int sender_socket){
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

//получаем имя
    bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if(bytes <= 0){
        close(client_sock);
        return NULL;
    }
    buffer[bytes] = '\0';
    strcpy(client_name, buffer);

//добавляем в массив
    pthread_mutex_lock(&client_mutex);
    clients[client_count].socket = client_sock;
    strcpy(clients[client_count].name, client_name);
    clients[client_count].active = 1;
    client_count++;
    pthread_mutex_unlock(&client_mutex);
    printf("[%s] Подключился/лась\n", client_name);

//добавляем сообщение о подключение в логи
    char log_buf[64];
    snprintf(log_buf, sizeof(log_buf), "User Connect: %s", client_name);
    log_server(log_buf);

//оповещение всех о входе 
    char join_msg[BUFFER_SIZE];
    snprintf(join_msg, sizeof(join_msg), " %s вошел/а в чат", client_name);
    broadcast_messenge("Система", join_msg, client_sock);

//добавляем в список пользователей
    add_registered_user(client_name);

//обработка сообщений от пользователя
    while(1){
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) break;
        buffer[bytes] = '\0';

        printf("DEBUG: Сервер получил от %s: %s\n", client_name, buffer);
//проверка существует ли пользователь
        if(strncmp(buffer, "CHECK_USER ", 11) == 0){
            char check_name[32];
            sscanf(buffer + 11, "%31s", check_name);
            if(user_exists(check_name)){
                send(client_sock, "USER_EXISTS", 11, 0);
            }
            else send(client_sock, "USER_NOT_EXISTS", 15, 0);
            continue;
        }

        if(strcmp(buffer, "ENTER_COMMON") == 0){
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    clients[i].in_common_chat = 1;
                    break;
                }
            }
            char enter_msg[BUFFER_SIZE];
            snprintf(enter_msg, sizeof(enter_msg), " %s вошёл в чат", client_name);
            broadcast_messenge("Система", enter_msg, client_sock);
            continue;
        }

        if(strcmp(buffer, "LEAVE_COMMON") == 0){
            for(int i = 0; i < client_count; i++){
                if(clients[i].socket == client_sock){
                    clients[i].in_common_chat = 0;
                    break;
                }
            }
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, sizeof(leave_msg), " %s вышел из чата", client_name);
            broadcast_messenge("Система", leave_msg, client_sock);
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
            for(int j = i; j<client_count - 1; j++){
                clients[j] = clients[j+1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&client_mutex);

//оповещение всех о выходе + добавление сообщения в логи
    char leave_msg[BUFFER_SIZE];
    snprintf(leave_msg, sizeof(leave_msg), " %s покинул/а чат", client_name);
    broadcast_messenge("Система", leave_msg, client_sock);
    printf("[%s] Отключился/лась\n", client_name);

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