#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>

#define PORT "8888"
#define BUFFER_SIZE 1024
char my_name[32];
int server_sock;
int in_chat = 0;
int receiving_history;
int current_chat_type = 0;
char current_chat_name[32];
char current_partner[32];

//очищение от лишних символов
void trim(char *str) {
    char *start = str;
    char *end;
    
    while(*start == ' ' || *start == '\n' || *start == '\r' || *start == '[') start++;
    if(*start == '\0') { str[0] = '\0'; return; }
    
    end = start + strlen(start) - 1;
    while(end > start && (*end == ' ' || *end == '\n' || *end == '\r' || *end == ']')) end--;
    
    int len = end - start + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

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
    char temp[32];
    while(fscanf(f, "%31s", temp) == 1){
    trim(temp);
    if(strlen(temp) > 0){
        strcpy(my_contacts[my_contact_count].name, temp);
        my_contact_count++;
    }
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
    char clean[32];
    strcpy(clean, name);
    trim(clean);
    if(strlen(clean) == 0) return;
    for(int i = 0; i < my_contact_count; i++){
        if(strcmp(my_contacts[i].name, clean) == 0){
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
//структура кэша
typedef struct{
    char chat_name[64];
    char messages[500][BUFFER_SIZE];
    int msg_count;
} ChatCache;

ChatCache chat_caches[50];
int chat_cache_count = 0;
int receiving_history = 0;

void get_cache_filename(const char *my_name, const char *partner, char *filename){
    snprintf(filename, 128, "%s_%s", my_name, partner);
}

void get_server_history_name(const char *name1, const char *name2, char *result) {
    if (strcmp(name1, name2) < 0)
        snprintf(result, 64, "%s_%s", name1, name2);
    else
        snprintf(result, 64, "%s_%s", name2, name1);
}

//история с сервера
void load_cache_from_file(const char *chat_name){
    char filename[128];
    snprintf(filename, sizeof(filename), "cache_%s.txt", chat_name);
    FILE *f = fopen(filename, "r");
    if(!f) return;

    int idx = -1;
    for(int i = 0; i < chat_cache_count; i++){
        if(strcmp(chat_caches[i].chat_name, chat_name) == 0){
            idx = i;
            break;
        }
    }

    if(idx == -1 && chat_cache_count < 50){
        idx = chat_cache_count;
        strcpy(chat_caches[idx].chat_name, chat_name);
        chat_caches[idx].msg_count = 0;
        chat_cache_count++;
    }

    if (idx != -1) {
        char line[BUFFER_SIZE];
        chat_caches[idx].msg_count = 0;
        while (fgets(line, sizeof(line), f) && chat_caches[idx].msg_count < 500) {
            line[strcspn(line, "\n")] = '\0';
            strcpy(chat_caches[idx].messages[chat_caches[idx].msg_count], line);
            chat_caches[idx].msg_count++;
        }
    }
    fclose(f);
}

//сохранение кэша в файл
void save_to_cache_file(const char *chat_name, const char *message){
    char filename[128];
    snprintf(filename, sizeof(filename), "cache_%s.txt", chat_name);
    FILE *f = fopen(filename, "a");
    if(f){
        fprintf(f, "%s\n", message);
        fclose(f);
    }
}

//вывод кэша
void show_cached_messages(const char *chat_name){
    for(int i = 0; i < chat_cache_count; i++){
        if(strcmp(chat_caches[i].chat_name, chat_name) == 0){
            for(int j = 0; j < chat_caches[i].msg_count; j++){
                printf("%s\n", chat_caches[i].messages[j]);
            }
            break;
        }
    }
}

//поток для приема сообщений
void *receive_message(void *arg){
    char buffer[BUFFER_SIZE];
    int bytes;

    while(1){
        bytes = recv(server_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) {
            if(bytes == 0) {
                printf("\nСоединение закрыто сервером\n");
            }
            close(server_sock);
            exit(1);
        }
        buffer[bytes] = '\0';

        if (strcmp(buffer, "HISTORY_START") == 0) {
            receiving_history = 1;
            continue;
        }
        if (strcmp(buffer, "HISTORY_END") == 0) {
            receiving_history = 0;
            show_cached_messages(current_chat_name);
            printf("> ");
            fflush(stdout);
            continue;
        }

        if (receiving_history) {
            save_to_cache_file(current_chat_name, buffer);
            continue;
        }
        if(strncmp(buffer, "[ЛС от ", 11) == 0)
        {
            char sender[32];
            char *start = buffer + 11;
            char *end = strchr(start, ']');
            if(end){
                int len = end - start;
                if(len > 31) len = 31;
                strncpy(sender, start, len);
                sender[len] = '\0';
            }
            trim(sender);
            add_my_contact(sender);

            char cache_filename[128];
            get_cache_filename(my_name, sender, cache_filename);
            save_to_cache_file(cache_filename, buffer);

            if(in_chat && current_chat_type == 2 && strcmp(current_partner, sender) == 0){
                printf("\r\033[K%s\n> ", buffer);
                fflush(stdout);
            }
        }
        
        else if(buffer[0] == '[' && strncmp(buffer, "[Система]", 9) != 0){
            if(in_chat && current_chat_type == 1){
                printf("\r\033[K%s\n> ", buffer);
                fflush(stdout);
            }
        }

        else if(strncmp(buffer, "[Система]", 9) == 0){
            if(in_chat && current_chat_type == 1 && strstr(buffer, "личный чат") == NULL){
                printf("\r\033[K%s\n> ", buffer);
                fflush(stdout);
            }

            else if(in_chat && current_chat_type == 2 && strstr(buffer, "личный чат") != NULL){
                printf("\r\033[K%s\n> ", buffer);
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
    
    load_cache_from_file("BROADCAST");
    printf("\n--- Общий чат ---\n");
    printf("Все пользователи видят сообщения\n");
    printf("Введите /exit для выхода\n\n");

    int cache_empty = 1;
    for(int i = 0; i < chat_cache_count; i++){
        if(strcmp(chat_caches[i].chat_name, "BROADCAST") == 0 && chat_caches[i].msg_count > 0){
            cache_empty = 0;
            break;
        }
    }
    if(cache_empty){
        send(server_sock, "GET_HISTORY BROADCAST", 21, 0);
    }
    else{
        show_cached_messages("BROADCAST");
    }
    send(server_sock, "ENTER_COMMON", 12, 0);

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

        int sent = send(server_sock, msg, strlen(msg), 0);
        char self_msg[BUFFER_SIZE];
        snprintf(self_msg, sizeof(self_msg), "[%s]: %s", my_name, msg);
        save_to_cache_file("BROADCAST", self_msg);
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
            printf("У вас нет контактов. Сначала отправте личное сообщение - @имя или /exit\n");
        }
        else{
            printf("\nВведите номер собеседника, @имя для добавления нового или /exit: ");
        }
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';

        if(strcmp(input, "/exit") == 0) return;
        if(input[0] == '@'){
            sscanf(input + 1, "%31s", recipient);
            trim(recipient);
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
                else if(strcmp(response, "USER_NOT_EXISTS") == 0){
                    printf("Пользователь %s не найден\n", recipient);
                    continue;
                }
            }
            else{
                printf("Ошибка при проверке пользователя\n");
                continue;
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

    strcpy(current_partner, recipient);
    char cache_filename[128];
    get_cache_filename(my_name, recipient, cache_filename);
    strcpy(current_chat_name, cache_filename);

    in_chat = 1;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ENTER_PRIVATE %s", recipient);
    send(server_sock, cmd, strlen(cmd), 0);

    char server_name[64];
    get_server_history_name(my_name, recipient, server_name);
    load_cache_from_file(cache_filename);
    
    printf("\n--- Личный чат с %s ---\n", recipient);
    printf("Введите /exit для выхода\n\n");

    int cache_empty = 1;
    for(int i = 0; i < chat_cache_count; i++){
        if(strcmp(chat_caches[i].chat_name, cache_filename) == 0 && chat_caches[i].msg_count > 0){
            cache_empty = 0;
            break;
        }
    }

    if(cache_empty){
        char get_cmd[64];
        snprintf(get_cmd, sizeof(get_cmd), "GET_HISTORY %s", server_name);
        send(server_sock, get_cmd, strlen(get_cmd), 0);
        usleep(200000);
    }
    else show_cached_messages(cache_filename);

    while(in_chat){
        printf("> ");
        fflush(stdout);
        fgets(msg, BUFFER_SIZE, stdin);
        msg[strcspn(msg, "\n")] = '\0';

        if(strcmp(msg, "/exit") == 0){
            send(server_sock, "LEAVE_PRIVATE", 13, 0);
            in_chat = 0;
            break;
        }

        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "@%s %s", recipient, msg);
        int sent = send(server_sock, buffer, strlen(buffer), 0);
        char self_msg[BUFFER_SIZE];
        snprintf(self_msg, sizeof(self_msg), "[ЛС от %s]: %s", my_name, msg);
        save_to_cache_file(cache_filename, self_msg);
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

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct linger lo = {1, 0};
    setsockopt(server_sock, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));

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
                close(server_sock);
                exit(0);
            }
            else printf("Неверный выбор\n");
        }
    }

    close(server_sock);
    return 0;
}