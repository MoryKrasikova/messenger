#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define PORT "8888"
#define BUFFER_SIZE 1024

int server_sock;

//поток для приема сообщений
void *receive_message(void *arg){
    char buffer[BUFFER_SIZE];
    int bytes;

    while(1){
        bytes = recv(server_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) {
            printf("\nСоединение потеряно\n");
            break;
        }
        buffer[bytes] = '\0';
        printf("%s\n", buffer);
        printf("> ");
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]){
    struct addrinfo hints, *res;
    char name[32];
    char message[BUFFER_SIZE];
    pthread_t recv_thread;

    if(argc != 2){
        printf("Использование: ./client <IP_сервера>\n");
        return 1;
    }

    printf("Введите ваше имя: ");
    fflush(stdout);
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = '\0';

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if(getaddrinfo(argv[1], PORT, &hints, &res) != 0){
        printf("Ошибка: неверный IP\n");
        return 1;
    }

//создание сокета
    server_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(server_sock == -1){
        perror("socket");
        freeaddrinfo(res);
        return 1;
    }

//подключение к серверу
    if(connect(server_sock, res->ai_addr, res->ai_addrlen) == -1){
        perror("connect");
        close(server_sock);
        freeaddrinfo(res);
        return 1;
    }

    freeaddrinfo(res);
//отправляем имя на сервер
    send(server_sock, name, strlen(name), 0);

    printf("\n==============================\n");
    printf("      Подключено к чату!\n");
    printf("      Ваше имя: %s\n", name);
    printf("      Для выхода напишите /exit\n");
    printf("\n==============================\n");

//запускаем поток для приема сообщений
    pthread_create(&recv_thread, NULL, receive_message, NULL);

//цикл для отправки сообщений
    while(1){
        printf("> ");
        fflush(stdout);
        fgets(message, BUFFER_SIZE, stdin);
        message[strcspn(message, "\n")] = '\0';

        if(strcmp(message, "/exit") == 0){
            send(server_sock, message, strlen(message), 0);
            break;
        }

        send(server_sock, message, strlen(message), 0);
    }

    close(server_sock);
    printf("Выход из чата...\n");
    return 0;
}