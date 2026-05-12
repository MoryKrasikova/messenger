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
char my_name[32];
int server_sock;
int in_chat = 0;
int current_chat_type = 0;
char current_chat_name[32];

//структура контактов
typedef struct{
    char name[32];
} Contact;

Contact my_contacts[100];
int my_contact_count = 0;
char my_contacts_file[64];

//имя файла с контактами
void set_contacts_filename(const char *username){
    snprintf(my_contacts_file, sizeof(my_contacts_file), "my_contacts_%s.txt", username);
}

//подсчет контактов
void load_my_contacts(){
    FILE *f = fopen(my_contacts_file, "r");
    if(!f) return;
    my_contact_count = 0;
    while(fscanf(f, "%31s\n", my_contacts[my_contact_count].name) == 1){
        my_contact_count++;
    }
}

//сохраняем контакты в файл
void save_my_contacts(){
    FILE *f = fopen(my_contacts_file, "w");
    if(!f) return;
    for(int i = 0; i < my_contact_count; i++){
        fprintf(f, "%s\n", my_contacts[i].name);
    }
    fclose(f);
}

//добавляем новый контакт
void add_my_contact(const char *name){
    for(int i = 0; i < my_contact_count; i++){
        if(strcmp(my_contacts[i].name, name) == 0){
            return;
        }
    }

    if(my_contact_count < 100){
        strcpy(my_contacts[my_contact_count].name, name);
        my_contact_count++;
        save_my_contacts();
    }
}

void show_my_contacts(){
    printf("\n--- Ваши контакты ---\n");
    if(my_contact_count == 0){
        return;
    }
    for(int i = 0; i < my_contact_count; i++){
        printf("%d. %s\n", i+1, my_contacts[i].name);
    }
}

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

        if(strncmp(buffer, "[ЛС от ", 11) == 0){
            char sender[64] = {0};

            char *start = buffer + 11;
            char *end = strchr(start, ']');
            if(end){
                int len = end - start;
                if(len>63) len = 63;
                strncpy(sender, start, len);
                sender[len] = '\0';
            }

            add_my_contact(sender);
            if(in_chat && current_chat_type == 2 && strcmp(current_chat_name, sender) == 0){
                printf("\r\033[K");
                printf("%s\n", buffer);
                printf("> ");
                fflush(stdout);
            }
        }
        else if(strncmp(buffer, "[Система]", 9) == 0){
            if(in_chat && current_chat_type == 1){
                printf("\r\033[K");
                printf("%s\n", buffer);
                printf("> ");
                fflush(stdout);
            }
        }
        else if(in_chat && current_chat_type == 1 && buffer[0] == '['){
            if(strncmp(buffer, "[ЛС от ", 11) != 0){
                printf("\r\033[K");
                printf("%s\n", buffer);
                printf("> ");
                fflush(stdout);
            }
        }
    }
    return NULL;
}


//общий чат
void enter_common_chat(){
    current_chat_type = 1;
    strcpy(current_chat_name, "Общий чат");
    char msg[BUFFER_SIZE];
    in_chat = 1;
    send(server_sock, "ENTER_COMMON", 12, 0);

    printf("\n--- Общий чат ---\n");
    printf("Все пользователи видят сообщения\n");
    printf("Введите /exit для выхода\n\n");

    while(in_chat){
        printf("> ");
        fflush(stdout);
        fgets(msg, BUFFER_SIZE, stdin);
        msg[strcspn(msg, "\n")] = '\0';

        if(strcmp(msg, "/exit") == 0){
            send(server_sock, "LEAVE_COMMON", 12, 0);
            in_chat = 0;
            break;
        }

        send(server_sock, msg, strlen(msg), 0);
    }

    current_chat_type = 0;
}

//личные чаты
void enter_private_chat(){
    char recipient[32];
    char msg[BUFFER_SIZE];
    char input[32];
    char response[32];
    while(1){
        show_my_contacts();

        if(my_contact_count == 0){
            printf("У вас нет контактов Сначала отправте личное сообщение - @имя или /exit\n");
        }
        else{
            printf("\nВведите номер собеседника, @имя для добавления нового или /exit: ");
        }
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';

        if(strcmp(input, "/exit") == 0) return;
        if(input[0] == '@'){
            sscanf(input + 1, "%31s", recipient);
//проверяем есть ли уже в контактах
            int already_contact = 0;
            for(int i = 0; i < my_contact_count; i++){
                if(strcmp(my_contacts[i].name, recipient) == 0){
                    already_contact = 1;
                    break;
                }
            }

            if(already_contact){
                break;
            }
//проверяем существует ли новый пользователь
            char command[64];
            snprintf(command, sizeof(command), "CHECK_USER %s", recipient);
            send(server_sock, command, strlen(command), 0);

            int bytes = recv(server_sock, response, sizeof(response) - 1, 0);
            if(bytes > 0){
                response[bytes] = '\0';
                if(strcmp(response, "USER_EXISTS") == 0){
                    add_my_contact(recipient);
                    break;
                }
                else{
                    printf("Пользователь %s не найден\n", recipient);
                    continue;
                }
            }
        }
        else{
            int num = atoi(input);

            if (num >= 1 && num <= my_contact_count) {
                strcpy(recipient, my_contacts[num-1].name);
                break;  // выходим из цикла, переходим к чату
            } else {
                printf("Неверный номер.\n");
            }
        }
    }
    current_chat_type = 2;
    strcpy(current_chat_name, recipient);

    in_chat = 1;
    printf("\n--- Личный чат с %s ---\n", recipient);
    printf("Введите /exit для выхода\n\n");

    while(in_chat){
        printf("> ");
        fflush(stdout);
        fgets(msg, BUFFER_SIZE, stdin);
        msg[strcspn(msg, "\n")] = '\0';

        if(strcmp(msg, "/exit") == 0){
            in_chat = 0;
            break;
        }

        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "@%s %s", recipient, msg);
        send(server_sock, buffer, strlen(buffer), 0);
    }

    current_chat_type = 0;
 }

//меню при входе
void show_menu(){
    printf("\n--------------------------------------\n");
    printf("        ЧАТ-КЛИЕНТ\n");
    printf("\n--------------------------------------\n");
    printf("1. Общий чат\n");
    printf("2. Личные сообщения\n");
    printf("3. Выйти\n");
    printf("\n--------------------------------------\n");
    printf("Выберите действие: ");
}

int main(int argc, char *argv[]){
    struct addrinfo hints, *res;
    char name[32];
    char choice[10];
    pthread_t recv_thread;

    if(argc != 2){
        printf("Использование: ./client <IP_сервера>\n");
        return 1;
    }

    printf("Введите ваше имя: ");
    fflush(stdout);
    fgets(name, sizeof(name), stdin);
    name[strcspn(name, "\n")] = '\0';
    strcpy(my_name, name);

    set_contacts_filename(name);
    load_my_contacts();

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
    printf("\n==============================\n");

//запускаем поток для приема сообщений
    pthread_create(&recv_thread, NULL, receive_message, NULL);

//цикл для отправки сообщений
    while(1){
        if(!in_chat){
            show_menu();
            fgets(choice, sizeof(choice), stdin);
            choice[strcspn(choice, "\n")] = '\0';

            if(strcmp(choice, "1") == 0){
                enter_common_chat();
            }
            else if(strcmp(choice, "2") == 0){
                enter_private_chat();
            }
            else if (strcmp(choice, "3") == 0){
                printf("Выход из чата\n");
                send(server_sock, "/exit", 5, 0);
                break;
            }
            else printf("Неверный выбор\n");
        }
    }

    close(server_sock);
    return 0;
}