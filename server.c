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
} Client;
//массив клиентов
Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

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

//обработка сообщений от пользователя
    while(1){
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) break;
        buffer[bytes] = '\0';

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

    while(1){
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }

    return 0;
}
