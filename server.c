#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT 8888

void *handle_client(void *arg){
    int client_fd = *(int*)arg;
    free(arg);
    printf("Клиент обрабатывается в потоке\n");
    close(client_fd);
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

        printf("Клиент подключился\n");
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }
    close(server_fd);

    return 0;
}
