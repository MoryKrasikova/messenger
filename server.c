#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

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
    char welcome_msg[BUFFER_SIZE];
    snprint(welcome_msg, sizeof(welcome_msg), "Добро пожаловать в чат, %s!", client_name);
    send(client_sock, welcome_msg, strlen(welcome_msg), 0);

//оповещение всех о входе 
    char join_msg[BUFFER_SIZE];
    snprintf(join_msg, sizeof(join_msg), " %s вошел/а в чат", client_name);
    broadcast_messenge("Система:", join_msg, client_sock);

//обработка сообщений от пользователя(exit)
    while(1){
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) break;

        if(strcmp(buffer, "/exit") == 0) break;
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

//оповещение всех о выходе
    char leave_msg[BUFFER_SIZE];
    snprintf(buffer, sizeof(leave_msg), " %s покинул/а чат", client_name);
    broadcast_messenge("Система:", leave_msg, client_sock);

    close(client_sock);
    return NULL;
}

int main(){
    int server_fd, *client_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    pthread_t thread;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Сервер слушает порт %d\n", PORT);

    while(1){
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }
    close(server_fd);

    return 0;
}
