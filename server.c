#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <locale.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <string.h>

int server_fd;
#define PORT 8888
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024


//структура клиента
typedef struct Client{
    int socket;
    char name[32];
    int active;
    int in_common_chat;
    char private_chat_with[32];
    char current_group[32];
    pthread_t thread;
    struct Client *next;
} Client;
//массив клиентов
Client *clients = NULL;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

//список всех пользователей, какие заходили в чат
typedef struct{
    char name[32];
    int exists;
} RegisteredUser;
RegisteredUser registered_users[100];
int registered_count = 0;
pthread_mutex_t registered_mutex = PTHREAD_MUTEX_INITIALIZER;

//структура группы
typedef struct GroupChat{
    char name[32];
    char creator[32];
    char members[50][32];
    int member_count;
    int active;
}GroupChat;
GroupChat groups[50];
int group_count = 0;
pthread_mutex_t group_mutex = PTHREAD_MUTEX_INITIALIZER;

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

//сохранение пользователя в файл
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

//добавление пользователя
void add_registered_user(const char *name){
    pthread_mutex_lock(&registered_mutex);
    if(user_exists(name)) { 
        pthread_mutex_unlock(&registered_mutex); 
        return; 
    }
    if(registered_count < 100){
        strcpy(registered_users[registered_count].name, name);
        registered_users[registered_count].exists = 1;
        registered_count++;
        save_registered_users();
    }

    pthread_mutex_unlock(&registered_mutex);
}

//уведомления в личных чатах
void notify_private_chat(const char *client_name, const char *partner, const char *status){
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "[Система]: %s %s личный чат", client_name, status);

    pthread_mutex_lock(&client_mutex);
    Client *cur = clients;
    while(cur){
        if(cur->active && strcmp(cur->name, partner) == 0 && strcmp(cur->private_chat_with, client_name) == 0){
            send(cur->socket, msg, strlen(msg), 0);
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);
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

//функция обработчик для завершения сервера
void handle_sigint(int sig){
    printf("\nВыключение сервера\n");
    log_server("Server Stop");
    close(server_fd);
    exit(0);
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
void send_chat_history(int client_sock, const char *current_user, const char *chat_name, int type) {
    char filename[128];
    if (type == 1) {
        snprintf(filename, sizeof(filename), "chat_broadcast.log");
    }
    else if (type == 2) {
        snprintf(filename, sizeof(filename), "group_%s.log", chat_name);
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
    Client *cur = clients;
    
    while(cur){
        if(cur->active && cur->socket != sender_socket){
            send(cur->socket, full_message, strlen(full_message), 0);
        }
        cur = cur->next;
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
    Client *cur = clients;
    
    while(cur){
        if(cur->active && strcasecmp(cur->name, recipient_name) == 0){
            send(cur->socket, full_message, strlen(full_message), 0);
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);
}

//сохранение истории группы
void save_group_message(const char *group_name, const char *from, const char *message){
    char filename[128];
    snprintf(filename, sizeof(filename), "group_%s.log", group_name);
    FILE *f = fopen(filename, "a");
    if (!f) return;
    fprintf(f, "[Группа %s][%s]: %s\n", group_name, from, message);
    fclose(f);
}


//проверка состоит ли пользователь в группе
int is_in_group(const char *group_name, const char *username){
    pthread_mutex_lock(&group_mutex);
    for(int i = 0; i < group_count; i++){
        if(groups[i].active && strcmp(groups[i].name, group_name) == 0){
            for(int j = 0; j < groups[i].member_count; j++){
                if(strcmp(groups[i].members[j], username) == 0){
                    pthread_mutex_unlock(&group_mutex);
                    return 1;
                }
            }
        }
    }
    pthread_mutex_unlock(&group_mutex);
    return 0;
}

//получение список групп пользователя
void get_user_groups(const char *username, char *response){
    response[0] = '\0';
    pthread_mutex_lock(&group_mutex);
    for(int i = 0; i < group_count; i++){
        if(groups[i].active){
            for(int j = 0; j < groups[i].member_count; j++){
                if(strcmp(groups[i].members[j], username) == 0){
                    char temp[64];
                    snprintf(temp, sizeof(temp), "%s\n", groups[i].name);
                    strcat(response, temp);
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&group_mutex);
}

//создание группы
int create_group(const char *group_name, const char *creator, int member_indices[], int count){
    pthread_mutex_lock(&group_mutex);
    if(group_count >= 50){
        pthread_mutex_unlock(&group_mutex);
        return -1;
    }

    for(int i = 0; i < group_count; i++){
        if(groups[i].active && strcmp(groups[i].name, group_name) == 0){
            pthread_mutex_unlock(&group_mutex);
            return -2;
        }
    }

    strcpy(groups[group_count].name, group_name);
    strcpy(groups[group_count].creator, creator);

    strcpy(groups[group_count].members[0], creator);
    groups[group_count].member_count = 1;

    for(int i = 0; i < count && groups[group_count].member_count < 50; i++){
        if(member_indices[i] >= 0 && member_indices[i]<registered_count){
            char *member_name = registered_users[member_indices[i]].name;
            if(strcmp(member_name, creator) != 0){
                int already = 0;
                for(int k = 0; k < groups[group_count].member_count; k++){
                    if(strcmp(groups[group_count].members[k], member_name) == 0){
                        already = 1;
                        break;
                    }
                }
                if(!already){
                    strcpy(groups[group_count].members[groups[group_count].member_count], member_name);
                    groups[group_count].member_count++;
                }
            }
        }
    }

    groups[group_count].active = 1;
    group_count++;

    pthread_mutex_unlock(&group_mutex);
    return 0;
}

//удаление группы
int delete_group(const char *group_name, const char *author){
    pthread_mutex_lock(&group_mutex);
    for(int i = 0; i < group_count; i++){
        if(groups[i].active && strcmp(groups[i].name, group_name) == 0){
            if(strcmp(groups[i].creator, author) != 0){
                pthread_mutex_unlock(&group_mutex);
                return -1;
            }

            groups[i].active = 0;

            char filename[128];
            snprintf(filename, sizeof(filename), "group_%s.log", group_name);
            remove(filename);

            pthread_mutex_unlock(&group_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&group_mutex);
    return -2;
}

//сообщения в группу
void send_group_message(const char *group_name, const char *sender, const char *message){
    save_group_message(group_name, sender, message);
    
    char full_msg[BUFFER_SIZE];
    snprintf(full_msg, sizeof(full_msg), "[Группа %s][%s]: %s", group_name, sender, message);
    
    pthread_mutex_lock(&client_mutex);
    Client *cur = clients;
    while(cur) {
        if(cur->active && strcmp(cur->current_group, group_name) == 0 && strcmp(cur->name, sender) != 0) {
            send(cur->socket, full_msg, strlen(full_msg), 0);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);
}

//уведомления группам
void send_group_notify(const char *group_name, const char *sender, const char *message) {
    printf("[DEBUG] send_group_notify: group='%s', sender='%s', msg='%s'\n", group_name, sender, message);
    pthread_mutex_lock(&client_mutex);
    Client *cur = clients;
    while(cur) {
        printf("[DEBUG] cur->name='%s', cur->current_group='%s', active=%d\n", cur->name, cur->current_group, cur->active);
        if(cur->active && strcmp(cur->current_group, group_name) == 0 && strcmp(cur->name, sender) != 0) {
            printf("[DEBUG] Отправляю %s (socket=%d)\n", cur->name, cur->socket);
            send(cur->socket, message, strlen(message), 0);
        }
        cur = cur->next;
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
    char log_buf[128];

//получаем имя
    bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if(bytes <= 0){
        close(client_sock);
        return NULL;
    }
    buffer[bytes] = '\0';
    trim(buffer);
    strcpy(client_name, buffer);

    pthread_mutex_lock(&client_mutex);

    Client *old = clients;
    while(old){
        if(old->active && strcmp(old->name, client_name) == 0){
            close(old->socket);
            old->active = 0;
            break;
        }
        old = old->next;
    }

    Client *new_client = (Client *)calloc(1, sizeof(Client));
    new_client->socket = client_sock;
    strcpy(new_client->name, client_name);
    new_client->active = 1;
    new_client->in_common_chat = 0;
    new_client->private_chat_with[0] = '\0';
    new_client->thread = pthread_self();

    new_client->next = clients;
    clients = new_client;

    pthread_mutex_unlock(&client_mutex);

    snprintf(log_buf, sizeof(log_buf), "User Connect: %s", client_name);
    log_server(log_buf);
    add_registered_user(client_name);

//обработка сообщений от пользователя
    while(1){
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if(bytes <= 0) break;

        buffer[bytes] = '\0';
        trim(buffer);

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
            int type = 0;
            if(strcmp(chat_name, "BROADCAST") == 0) {
                type = 1;
            } else if(strncmp(chat_name, "group:", 6) == 0) {
                type = 2;
                memmove(chat_name, chat_name + 6, strlen(chat_name) - 5);
            }
            send_chat_history(client_sock, client_name, chat_name, type);
            continue;
        }
//уведомления о входах в чаты
        if (strcmp(buffer, "ENTER_COMMON") == 0) {
            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while (cur) {
                if (cur->socket == client_sock) {
                    cur->in_common_chat = 1;
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            char enter_msg[BUFFER_SIZE];
            snprintf(enter_msg, sizeof(enter_msg), " %s вошёл/а в чат", client_name);
            broadcast_messenge("Система", enter_msg, client_sock);
            continue;
        }

        if (strncmp(buffer, "ENTER_PRIVATE ", 14) == 0) {
            char partner[32];
            sscanf(buffer + 14, "%31s", partner);
            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while (cur) {
                if (cur->socket == client_sock) {
                    strcpy(cur->private_chat_with, partner);
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            cur = clients;
            while (cur) {
                if (cur->active && strcmp(cur->name, partner) == 0) {
                    notify_private_chat(client_name, partner, "вошёл/а в");
                    break;
                }
                cur = cur->next;
            }
            continue;
        }

        if(strncmp(buffer, "ENTER_GROUP ", 12) == 0){
            char group_name[32];
            sscanf(buffer+12, "%31s", group_name);

            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while(cur){
                if(cur->socket == client_sock){
                    strcpy(cur->current_group, group_name);
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            char notify[BUFFER_SIZE];
            snprintf(notify, sizeof(notify), "[Система]: %s вошел/а в группу %s", client_name, group_name);
            send_group_notify(group_name, client_name, notify);
            continue;
        }

//уведомления о выходах из чатов
        if (strcmp(buffer, "LEAVE_COMMON") == 0) {
            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while (cur) {
                if (cur->socket == client_sock) {
                    cur->in_common_chat = 0;
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            char leave_msg[BUFFER_SIZE];
            snprintf(leave_msg, sizeof(leave_msg), " %s вышел/а из чата", client_name);
            broadcast_messenge("Система", leave_msg, client_sock);
            continue;
        }

        if (strcmp(buffer, "LEAVE_PRIVATE") == 0) {
            char partner[32] = "";
            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while (cur) {
                if (cur->socket == client_sock) {
                    strcpy(partner, cur->private_chat_with);
                    cur->private_chat_with[0] = '\0';
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            if (strlen(partner) > 0) {
                notify_private_chat(client_name, partner, "вышел/а из");
            }
            continue;
        }

        if(strcmp(buffer, "LEAVE_GROUP") == 0){
            char group_name[32] = "";

            pthread_mutex_lock(&client_mutex);
            Client *cur = clients;
            while(cur){
                if(cur->socket == client_sock){
                    strcpy(group_name, cur->current_group);
                    cur->current_group[0] = '\0';
                    break;
                }
                cur = cur->next;
            }
            pthread_mutex_unlock(&client_mutex);
            if (strlen(group_name) > 0) {
                char notify[BUFFER_SIZE];
                snprintf(notify, sizeof(notify), "[Система]: %s вышел/а из группы %s", client_name, group_name);
                send_group_notify(group_name, client_name, notify);
            }
            continue;
        }

        if(strcmp(buffer, "/exit") == 0) break;

//список пользователей для групп
        if(strcmp(buffer, "GET_USERS") == 0) {
            char user_list[BUFFER_SIZE] = "[GROUP]";
            int other_count = 0;
            for(int i = 0; i < registered_count; i++) {
                if(strcmp(registered_users[i].name, client_name) != 0) {  
                    char temp[64];
                    snprintf(temp, sizeof(temp), "%s\n", registered_users[i].name);
                    strcat(user_list, temp);
                    other_count++;
                }
            }
            if(other_count == 0) {
                strcat(user_list, "Нет других пользователей для добавления\n");
            }
            send(client_sock, user_list, strlen(user_list), 0);
            continue;
        }

//список групп пользователя
        if(strcmp(buffer, "GET_MY_GROUPS") == 0) {
            char temp_list[BUFFER_SIZE] = "";
            get_user_groups(client_name, temp_list);
            char group_list[BUFFER_SIZE] = "[GROUP]";
            if(strlen(temp_list) == 0) {
                strcat(group_list, "У вас нет групп\n");
            } else {
                strcat(group_list, temp_list);
            }
            send(client_sock, group_list, strlen(group_list), 0);
            continue;
        }

//создание группы
        //создание группы
        if(strncmp(buffer, "CREATE_GROUP ", 13) == 0){
            char group_name[32], members_str[BUFFER_SIZE];
            if(sscanf(buffer+13, "%31s %[^\n]", group_name, members_str) == 2){
                int indices[50], cnt = 0;
                char *token = strtok(members_str, ",");
                
                while(token && cnt < 50){
                    // Ищем пользователя по имени
                    int found = -1;
                    for(int i = 0; i < registered_count; i++){
                        if(strcmp(registered_users[i].name, token) == 0 && 
                           strcmp(registered_users[i].name, client_name) != 0){
                            found = i;
                            break;
                        }
                    }
                    if(found != -1){
                        // Проверка на дубликаты
                        int dup = 0;
                        for(int j = 0; j < cnt; j++){
                            if(indices[j] == found){ dup = 1; break; }
                        }
                        if(!dup) indices[cnt++] = found;
                    }
                    token = strtok(NULL, ",");
                }
                
                if(cnt == 0){
                    char msg[] = "[GROUP][Система]: Не указаны участники\n";
                    send(client_sock, msg, strlen(msg), 0);
                    continue;
                }
                
                int result = create_group(group_name, client_name, indices, cnt);
                if(result == 0){
                    char ok[BUFFER_SIZE];
                    snprintf(ok, sizeof(ok), "[GROUP][Система]: Группа '%s' создана\n", group_name);
                    send(client_sock, ok, strlen(ok), 0);
                } else if(result == -2){
                    char msg[] = "[GROUP][Система]: Группа с таким именем уже существует\n";
                    send(client_sock, msg, strlen(msg), 0);
                }
            }
            continue;
        }

//удаление группы
        if(strncmp(buffer, "DELETE_GROUP ", 13) == 0) {
            char group_name[32];
            sscanf(buffer + 13, "%31s", group_name);
            int result = delete_group(group_name, client_name);
            if(result == 0) {
                char ok[BUFFER_SIZE];
                snprintf(ok, sizeof(ok), "[GROUP][Система]: Группа '%s' удалена\n", group_name);
                send(client_sock, ok, strlen(ok), 0);
            } else if(result == -1) {
                char msg[] = "[GROUP][Система]: Вы не создатель этой группы\n";
                send(client_sock, msg, strlen(msg), 0);
            }
            continue;
        }


//сообщения в группу
        if(buffer[0] == '#'){
            char group_name[32], msg[BUFFER_SIZE];
            if(sscanf(buffer+1, "%31s %[^\n]", group_name, msg) == 2){
                if(is_in_group(group_name, client_name)){
                    send_group_message(group_name, client_name, msg);
                }
                continue;
            }
        }

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
    Client *prev = NULL, *cur = clients;
    while (cur) {
        if (cur->socket == client_sock) {
            if (prev) prev->next = cur->next;
            else clients = cur->next;
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&client_mutex);

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

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);
    log_server("Server Start");
    printf("Сервер слушает порт %d\n", PORT);


    load_registered_users();

    while(1){
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        if (*client_fd == -1) {
            free(client_fd);
            continue;
        }
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread); 
    }

    return 0;
} 