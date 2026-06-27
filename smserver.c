/*

Name : Arnav Priyadarshi
Roll : 23CS30008

*/

#include <stdio.h>  
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>

#define MAX_USERS 1000
#define BODY_LIMIT 65536
#define maxlen  1024

typedef struct user{
    char *name, *password ;
    int next_id ;
} users ;

typedef struct node{
    int val ;
    struct node *next ;
} node ;

typedef struct details{
    char *name, *subject ;
    node *list ;
    char *body ;
    int bytes_body ;

    int n_auths_failed, user_id ;
    char nonce[9] ;
} client_ ;


void str_tolower(char *str){
    for(int i = 0 ; str[i] ; i++){
        str[i] = tolower((unsigned char)str[i]) ;
    }
}

void create_dir(const char *path){
    if(mkdir(path, 0777) == -1){
        if(errno != EEXIST){
            perror("Failed to create directory") ;
        }
    }
}

int load_users_and_mailboxes(const char *filename, users user_array[]){
    FILE *file = fopen(filename, "r") ;
    if(!file){
        perror("Error opening user file") ;
        exit(1) ;
    }

    create_dir("mailboxes") ;

    char line[256] ;
    int count = 0 ;

    while(fgets(line, sizeof(line), file) && count < MAX_USERS){
        char u[100], p[100] ;
        
        if(sscanf(line, "%99s %99s", u, p) == 2){
            str_tolower(u) ;
            
            user_array[count].name = strdup(u) ;
            user_array[count].password = strdup(p) ;

            char path[256] ;
            snprintf(path, sizeof(path), "mailboxes/%s", u) ;
            create_dir(path) ;

            char meta_path[300] ;
            snprintf(meta_path, sizeof(meta_path), "%s/id_counter.meta", path) ;
            FILE *meta = fopen(meta_path, "r") ;

            if(meta){
                // Reading the metadata file for getting the next_id
                if(fscanf(meta, "%d", &user_array[count].next_id) != 1){
                    user_array[count].next_id = 1 ;
                }
                fclose(meta) ;
            }
            else{
                // If metadata file does not exist, checking each file in the directory
                int max_id = 0 ;
                DIR *dir = opendir(path) ;
                if(dir){
                    struct dirent *entry ;
                    while((entry = readdir(dir)) != NULL){
                        int file_id ;
                        if(sscanf(entry->d_name, "%d.txt", &file_id) == 1){
                            if(file_id > max_id) max_id = file_id ;
                        }
                    }
                    closedir(dir) ;
                }
                user_array[count].next_id = max_id + 1 ;
                
                meta = fopen(meta_path, "w") ;
                if(meta){
                    fprintf(meta, "%d", user_array[count].next_id) ;
                    fclose(meta) ;
                }
            }

            count++ ;
        }
    }
    
    fclose(file) ;
    return count ;
}

void get_timestamp(char *buffer){
    time_t rawtime ;
    struct tm *timeinfo ;
    time(&rawtime) ;
    timeinfo = localtime(&rawtime) ;
    strftime(buffer, 30, "[%Y-%m-%d %H:%M:%S]", timeinfo) ;
}

int write_all(int sockfd, char *buffer, int len){
    int total_sent = 0, total_left = len ;
    while(total_sent < len){
        int n = write(sockfd, buffer+total_sent, total_left) ;
        if(n == -1) return n ;
        if(n == 0) return n ;
        total_sent += n ;
        total_left -= n ;
    }
    return total_sent ;
}

int read_all(int sockfd, char buffer[]){
    int t = 0 ;
    char c ;
    while(t < maxlen - 1){
        int n = read(sockfd, &c, 1) ;
        if(n <= 0) return n ; 
        buffer[t++] = c ;
        if(c == '\n') break ;
    }
    buffer[t] = '\0' ; 
    return t ;
}

void init_client(client_ *client){
    free(client->name) ;
    free(client->subject) ;
    free(client->body) ;
    
    node *curr = client->list ;
    while(curr != NULL){
        node *temp = curr ;
        curr = curr->next ;
        free(temp) ;
    }
    
    client->list = NULL ;
    client->name = NULL ;
    client->subject = NULL ;
    client->body = NULL ;
    client->bytes_body = 0 ;
    client->n_auths_failed = 0 ;
    client->user_id = -1 ;
}

void init_nonce(client_ *client){
    for(int i = 0 ; i < 8 ; i++){
        int x = rand() % 62 ;
        if(x < 26) client->nonce[i] =(char)('a' + x) ;
        else if(x < 52) client->nonce[i] =(char)('A' + x - 26) ;
        else client->nonce[i] =(char)('0' + x - 52) ;
    }
    client->nonce[8] = '\0' ;
}

int proc_command(char *msg){
    char m[16] = {0} ;
    int i = 0 ;
    while(msg[i] != ' ' && msg[i] != '\r' && msg[i] != '\n' && msg[i] != '\0' && i < 15){
        m[i] = msg[i] ;
        i++ ;
    }
    m[i] = '\0' ;
    
    if(!strcmp(msg, "MODE SEND")) return 0 ;
    if(!strcmp(m, "FROM")) return 1 ;
    if(!strcmp(m, "TO")) return 2 ;
    if(!strcmp(m, "SUB")) return 3 ;
    if(!strcmp(m, "BODY")) return 4 ;

    if(!strcmp(msg, "MODE RECV")) return 6 ;
    if(!strcmp(m, "AUTH")) return 7 ;
    if(!strcmp(m, "LIST")) return 8 ;
    if(!strcmp(m, "READ")) return 9 ;
    if(!strcmp(m, "DELETE")) return 10 ;
    if(!strcmp(m, "COUNT")) return 11 ;

    if(!strncmp(msg, "MODE ", 5)) return 15 ;

    return 100 ;
}

char *proc_name(char *buf){
    char *name =(char *)malloc(512*sizeof(char)) ;
    int j = 0, n = strlen(buf) ;
    
    while(j < n && buf[j] != ' '){
        j++ ;
    }

    if(j < n){
        strcpy(name, buf + j + 1) ;
    } else {
        name[0] = '\0' ;
    }
    return name ;
}

void bad_seq(int state[], client_ client[], char msg[], int fd){
    char ts[30] ;
    get_timestamp(ts) ;
    
    sprintf(msg, "ERR Bad sequence\r\n") ;
    int n = write_all(fd, msg, strlen(msg)) ;
    if(n <= 0){
        if(n < 0) perror("write") ;
        printf("%s Client on fd %d disconnected\n", ts, fd) ;
    }
    state[fd] = -2 ;
    init_client(&client[fd]) ; 
}

int find(char *username, users user[]){
    // Helper function to check whether a username exists in the given users list
    for(int i = 0 ; i < MAX_USERS ; i++){
        if(user[i].name != NULL && !strcmp(user[i].name, username)){
            return i ;
        }
    }
    return -1 ;
}

unsigned long djb2(const char *str){
    unsigned long hash = 5381 ;
    int c ;
    while((c = *str++)){
        hash =((hash << 5) + hash) + c ; 
    }
    return hash ;
}

int is_match(users user[], char *buffer, char nonce[]){
    // Helper function to check whether the passwords match
    int j = 5, n = strlen(buffer), k = 5 ;
    while(j < n && buffer[j] != ' ') j++ ;

    char username[21] ;
    while(k < j){
        username[k-5] = buffer[k] ;
        k++ ;
    }
    username[k-5] = '\0' ;
    
    str_tolower(username) ;

    char hash[30] ;
    strcpy(hash, buffer+k+1) ;

    unsigned long val1 = strtoul(hash, NULL, 10) ;

    for(int i = 0 ; i < MAX_USERS ; i++){
        if(user[i].name == NULL) continue ;
        if(strcmp(username, user[i].name)) continue ;

        char str[150] ;
        sprintf(str, "%s%s", user[i].password, nonce) ;
        unsigned long hash_pass = djb2(str) ;

        if(val1 == hash_pass) return i ;
        break ;
    }

    return -1 ;
}

int main(int argc, char* argv[]){

    srand(getpid()) ;

    if(argc < 3){
        printf("Error: provide port and userfile\n./server <port> <user_filename>\n") ;
        exit(1) ;
    }

    int port = atoi(argv[1]) ;
    char *filename = argv[2] ; 
    char ts[30] ; 

    int listen_fd, client_fd ;
    struct sockaddr_in server_addr, client_addr ;
    socklen_t len = sizeof(client_addr) ;

    fd_set master_set, read_set ;
    int max_fd ;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0) ;
    if(listen_fd < 0){
        perror("Socket failed") ;
        exit(1) ;
    }

    // To prevent "port already in use" error
    int opt = 1 ;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ;

    memset(&server_addr, 0, sizeof(server_addr)) ;
    server_addr.sin_family = AF_INET ;
    server_addr.sin_port = htons(port) ; 
    server_addr.sin_addr.s_addr = INADDR_ANY ;

    if(bind(listen_fd,(struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Bind failed\n") ; exit(1) ;
    }

    if(listen(listen_fd, 5) < 0){
        perror("Listen failed\n") ; exit(1) ;
    }

    get_timestamp(ts) ;
    printf("%s Server started on port %d\n", ts, port) ;
    FD_ZERO(&master_set) ;
    FD_SET(listen_fd, &master_set) ;
    max_fd = listen_fd ;

    char msg[524], buffer[1036] ;
    users user[MAX_USERS] ;
    
    for(int i = 0 ; i < MAX_USERS ; i++){
        user[i].name = NULL ;
        user[i].password = NULL ;
    }
    
    int loaded_users = load_users_and_mailboxes(filename, user) ;
    get_timestamp(ts) ;
    printf("%s Loaded %d users from %s\n", ts, loaded_users, filename) ;
    
    int state[MAX_USERS] ; 
    client_ client[MAX_USERS] ;

    for(int i = 0 ; i < MAX_USERS ; i++){
        state[i] = -2 ;
        client[i].name = NULL ;
        client[i].subject = NULL ;
        client[i].body = NULL ;
    } 

    while(1){
        read_set = master_set ;
        int ready = select(max_fd + 1, &read_set, NULL, NULL, NULL) ;
        if(ready < 0){
            perror("Select failed\n") ;
            break ;
        }

        // accepting new connections
        if(FD_ISSET(listen_fd, &read_set)){
            client_fd = accept(listen_fd,(struct sockaddr *)&client_addr, &len) ;

            if(client_fd < 0){
                perror("Accept failed") ;
            }
            else{
                state[client_fd] = -1 ;
                char client_ip[16] ;
                inet_ntop(AF_INET,(struct sockaddr *)&(client_addr), client_ip, sizeof(client_addr)) ;

                get_timestamp(ts) ;
                printf("%s New connection from %s:%d\n", ts, client_ip, ntohs(client_addr.sin_port)) ;
                
                sprintf(msg, "WELCOME SimpleMail v1.0\r\n") ;
                write_all(client_fd, msg, strlen(msg)) ;

                FD_SET(client_fd, &master_set) ;
                if(client_fd > max_fd) max_fd = client_fd ;
            }
        }

        for(int fd = 0 ; fd <= max_fd ; fd++){
            if(fd == listen_fd) continue ;
            if(!FD_ISSET(fd, &read_set)) continue ;

            int n = read_all(fd, buffer) ;
            
            get_timestamp(ts) ; 

            if(n <= 0){
                if(n < 0) perror("read") ;
                printf("%s Client on fd %d disconnected\n", ts, fd) ;
                close(fd) ;
                FD_CLR(fd, &master_set) ;
                init_client(&client[fd]) ;
                state[fd] = -2 ;
                continue ;
            }

            buffer[n-2] = '\0' ; 
            // Taking Body text from Client
            if(state[fd] == 4){
                n -= 2 ; 
                
                if(n == 1 && buffer[0] == '.'){
                    // If the body text is completed, storing text files in mailboxes
                    int y = client[fd].bytes_body ;
                    client[fd].body[y] = '\0' ;

                    char email_date[30] ;
                    time_t rawtime ;
                    struct tm *timeinfo ;
                    time(&rawtime) ;
                    timeinfo = localtime(&rawtime) ;
                    strftime(email_date, 30, "%Y-%m-%d %H:%M:%S", timeinfo) ;

                    // getting the list of recipients
                    char to_list[1024] = {0} ;
                    node *list_curr = client[fd].list ;
                    while(list_curr != NULL){
                        strcat(to_list, user[list_curr->val].name) ;
                        if(list_curr->next != NULL){
                            strcat(to_list, ", ") ;
                        }
                        list_curr = list_curr->next ;
                    }

                    int r_count = 0 ;
                    node *curr = client[fd].list ;
                    
                    while(curr != NULL){
                        int user_index = curr->val ;
                        
                        char filepath[512] ;
                        snprintf(filepath, sizeof(filepath), "mailboxes/%s/%d.txt", user[user_index].name, user[user_index].next_id) ;
                        
                        FILE *mail_file = fopen(filepath, "w") ;
                        if(mail_file){
                            // Writing mail in the respective mailbox
                            fprintf(mail_file, "From: %s\n", client[fd].name) ;
                            fprintf(mail_file, "To: %s\n", to_list) ;
                            fprintf(mail_file, "Subject: %s\n", client[fd].subject) ;
                            fprintf(mail_file, "Date: %s\n", email_date) ;
                            fprintf(mail_file, "---\n") ; 
                            fprintf(mail_file, "%s", client[fd].body) ;
                            
                            fclose(mail_file) ;
                            
                            user[user_index].next_id++ ; 
                            r_count++ ;

                            // Updating the metadata file
                            char meta_path[512] ;
                            snprintf(meta_path, sizeof(meta_path), "mailboxes/%s/id_counter.meta", user[user_index].name) ;
                            FILE *meta = fopen(meta_path, "w") ;
                            if (meta) {
                                fprintf(meta, "%d", user[user_index].next_id) ;
                                fclose(meta) ;
                            }

                        } else {
                            perror("Failed to write mail file") ;
                        }
                        curr = curr->next ;
                    }

                    get_timestamp(ts) ;
                    printf("%s Mail delivered from \"%s\" to [%s] (%d recipient%s)\n", ts, client[fd].name, to_list, r_count, r_count == 1 ? "" : "s") ;

                    sprintf(msg, "OK Delivered to %d mailboxes\r\n", r_count) ;
                    write_all(fd, msg, strlen(msg)) ;
                    
                    state[fd] = 0 ; // changing state to 0 so that a client can send either a FROM or QUIT command
                }
                else{
                    int y = client[fd].bytes_body ;
    
                    if(buffer[0] == '.'){
                        n-- ; 
                        if(y+n > BODY_LIMIT){
                            sprintf(msg, "ERR Body too large\r\n") ;
                            write_all(fd, msg, strlen(msg)) ;
                            close(fd) ;
                            FD_CLR(fd, &master_set) ;
                            init_client(&client[fd]) ;
                            continue ;
                        }
                        strcpy(client[fd].body+y, buffer+1) ;
                    }
                    else{
                        if(y+n > BODY_LIMIT){
                            sprintf(msg, "ERR Body too large\r\n") ;
                            write_all(fd, msg, strlen(msg)) ;
                            close(fd) ;
                            FD_CLR(fd, &master_set) ;
                            init_client(&client[fd]) ;
                            continue ;
                        }
                        strcpy(client[fd].body+y, buffer) ;
                    }
                    y += n ;
                    client[fd].body[y++] = '\n' ; 
                    client[fd].bytes_body = y ;
                }
                continue ;
            }

            int command = proc_command(buffer) ;

            if(command == 0){
                // MODE SEND
                if(state[fd] != -1){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                state[fd] = 0 ;
                get_timestamp(ts) ;
                printf("%s Client on fd %d selected MODE SEND\n", ts, fd) ;
                sprintf(msg, "OK\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
            }
            else if(command == 1){
                // FROM command
                if(state[fd] != 0){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                init_client(&client[fd]) ;
                state[fd] = 1 ;
                client[fd].name = proc_name(buffer) ; // getting the display name of sender
                sprintf(msg, "OK Sender accepted\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
            }
            else if(command == 2){
                // TO command
                if(state[fd] != 1 && state[fd] != 2){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                state[fd] = 2 ;
                char *username = proc_name(buffer) ;
                
                str_tolower(username) ; // usernames are case-insensitive
                
                int x = find(username, user) ; // checking whether username exists
                if(x == -1){
                    sprintf(msg, "ERR No such user\r\n") ;
                }
                else{
                    node *head = client[fd].list ;
                    node *temp =(node *)malloc(sizeof(node)) ;
                    temp->val = x ;
                    temp->next = head ;
                    client[fd].list = temp ;
                    sprintf(msg, "OK Recipient accepted\r\n") ;
                }
                write_all(fd, msg, strlen(msg)) ;
                free(username) ; 
            }
            else if(command == 3){
                // SUB command
                if(state[fd] != 2){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                if(client[fd].list == NULL){
                    // If no registered users are selected in TO command, then client can't proceed
                    sprintf(msg, "ERR No valid recipients\r\n") ;
                    write_all(fd, msg, strlen(msg)) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    init_client(&client[fd]) ;
                    continue ;
                }

                state[fd] = 3 ;
                char *subject = proc_name(buffer) ;
                if(strlen(subject) == 0){
                    // empty subject
                    free(subject) ;
                    subject = strdup("(no subject)") ;
                }
                client[fd].subject = subject ;
                sprintf(msg, "OK Subject accepted\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
            }
            else if(command == 4){
                // BODY command
                if(state[fd] != 3){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }

                state[fd] = 4 ;
                sprintf(msg, "OK Send body, end with CRLF.CRLF\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
                client[fd].body =(char *)malloc((BODY_LIMIT+1)*sizeof(char)) ;
                client[fd].bytes_body = 0 ;
            }
            else if(!strcmp(buffer, "QUIT")){
                if(state[fd] != 0 && state[fd] != 7){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }

                sprintf(msg, "BYE\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
                get_timestamp(ts) ;
                printf("%s Client on fd %d disconnected (via QUIT)\n", ts, fd) ;
                close(fd) ;
                FD_CLR(fd, &master_set) ;
                init_client(&client[fd]) ;
                state[fd] = -2 ;
            }
            else if(command == 6){
                // MODE RECV
                if(state[fd] != -1){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                state[fd] = 6 ;
                init_client(&client[fd]) ;
                init_nonce(&client[fd]) ; // generating nonce
                
                get_timestamp(ts) ;
                printf("%s Client on fd %d selected MODE RECV\n", ts, fd) ;

                sprintf(msg, "OK\r\n") ;
                write_all(fd, msg, strlen(msg)) ;

                sprintf(msg, "AUTH REQUIRED %s\r\n", client[fd].nonce) ;
                write_all(fd, msg, strlen(msg)) ;
            }
            else if(command == 7){
                // AUTH command
                if(state[fd] != 6){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }

                // checking whether passwords match
                int x = is_match(user, buffer, client[fd].nonce) ;
                if(x == -1){
                    // passwords did not match
                    client[fd].n_auths_failed++ ;
                    if(client[fd].n_auths_failed >= 3){
                        // Limit of number of trials exceeded
                        sprintf(msg, "ERR Too many failures\r\n") ;
                        write_all(fd, msg, strlen(msg)) ;

                        get_timestamp(ts) ;
                        printf("%s Client on fd %d disconnected (Too many auth failures)\n", ts, fd) ;
                        
                        close(fd) ;
                        FD_CLR(fd, &master_set) ;
                        init_client(&client[fd]) ;
                        state[fd] = -2 ;
                        continue ;
                    }

                    sprintf(msg, "ERR Authentication failed\r\n") ;
                    write_all(fd, msg, strlen(msg)) ;
                    continue ;
                }

                client[fd].user_id = x ;
                state[fd] = 7 ;

                get_timestamp(ts) ;
                printf("%s Authentication successful for user %s\n", ts, user[x].name) ;

                sprintf(msg, "OK Welcome %s\r\n", user[x].name) ;
                write_all(fd, msg, strlen(msg)) ;
            }
            else if(command >= 8 && command <= 11){
                if(state[fd] != 7){
                    bad_seq(state, client, msg, fd) ;
                    close(fd) ;
                    FD_CLR(fd, &master_set) ;
                    continue ;
                }
                
                char path[256] ;
                snprintf(path, sizeof(path), "mailboxes/%s", user[client[fd].user_id].name) ;
                
                if(command == 11){
                    // COUNT
                    int msg_count = 0 ;
                    DIR *dir = opendir(path) ;
                    if(dir){
                        // opening directory and traversing it to get the count of .txt files
                        struct dirent *entry ;
                        while((entry = readdir(dir)) != NULL){
                            int file_id ;
                            if(sscanf(entry->d_name, "%d.txt", &file_id) == 1) msg_count++ ;
                        }
                        closedir(dir) ;
                    }
                    sprintf(msg, "OK %d\r\n", msg_count) ;
                    write_all(fd, msg, strlen(msg)) ;
                }
                else if(command == 8){
                    // LIST
                    int ids[1000] ;
                    int msg_count = 0 ;
                    DIR *dir = opendir(path) ;
                    if(dir){
                        struct dirent *entry ;
                        while((entry = readdir(dir)) != NULL && msg_count < 1000){
                            int file_id ;
                            if(sscanf(entry->d_name, "%d.txt", &file_id) == 1){
                                ids[msg_count++] = file_id ;
                            }
                        }
                        closedir(dir) ;
                    }
                    
                    // sorting text files according to their ID number
                    for(int i=0 ; i<msg_count-1 ; i++){
                       for(int j=0 ; j<msg_count-i-1 ; j++){
                           if(ids[j] > ids[j+1]){
                               int tmp = ids[j] ; ids[j] = ids[j+1] ; ids[j+1] = tmp ;
                           }
                       }
                    }

                    sprintf(msg, "OK %d messages\r\n", msg_count) ;
                    write_all(fd, msg, strlen(msg)) ;
                    
                    for(int i = 0 ; i < msg_count ; i++){
                        char filepath[512] ;
                        snprintf(filepath, sizeof(filepath), "%s/%d.txt", path, ids[i]) ;
                        FILE *f = fopen(filepath, "r") ;
                        if(f){
                            char from[100]="", sub[100]="", date[100]="" ;
                            char line_f[514] ;
                            
                            while(fgets(line_f, sizeof(line_f), f)){
                                if(strncmp(line_f, "---", 3) == 0) break ;
                                if(strncmp(line_f, "From: ", 6)==0){
                                    strcpy(from, line_f+6) ;
                                    from[strcspn(from, "\r\n")] = '\0' ;
                                }
                                else if(strncmp(line_f, "Subject: ", 9)==0){
                                    strcpy(sub, line_f+9) ;
                                    sub[strcspn(sub, "\r\n")] = '\0' ;
                                }
                                else if(strncmp(line_f, "Date: ", 6)==0){
                                    strcpy(date, line_f+6) ;
                                    date[strcspn(date, "\r\n")] = '\0' ;
                                }
                            }
                            fclose(f) ;
                            sprintf(msg, "%d\t%s\t%s\t%s\r\n", ids[i], from, sub, date) ;
                            write_all(fd, msg, strlen(msg)) ;
                        }
                    }
                    write_all(fd, ".\r\n", 3) ; // dot termination
                }
                else if(command == 9){
                    // READ
                    int msg_id ;
                    if(sscanf(buffer, "READ %d", &msg_id) == 1){
                        char filepath[512] ;
                        snprintf(filepath, sizeof(filepath), "%s/%d.txt", path, msg_id) ;
                        FILE *f = fopen(filepath, "r") ;
                        
                        if(f){
                            write_all(fd, "OK\r\n", 4) ;
                            char line_f[512] ;
                            while(fgets(line_f, sizeof(line_f), f)){
                                line_f[strcspn(line_f, "\r\n")] = '\0' ; 
                                
                                if(line_f[0] == '.') sprintf(msg, ".%s\r\n", line_f) ;
                                else sprintf(msg, "%s\r\n", line_f) ;
                                
                                write_all(fd, msg, strlen(msg)) ;
                            }
                            write_all(fd, ".\r\n", 3) ;
                            fclose(f) ;
                            
                            get_timestamp(ts) ;
                            printf("%s User %s READ message %d\n", ts, user[client[fd].user_id].name, msg_id) ;
                        }
                        else{
                            sprintf(msg, "ERR No such message\r\n") ;
                            write_all(fd, msg, strlen(msg)) ;
                        }
                    }
                }
                else if(command == 10){
                    // DELETE
                    int msg_id ;
                    if(sscanf(buffer, "DELETE %d", &msg_id) == 1){
                        char filepath[512] ;
                        snprintf(filepath, sizeof(filepath), "%s/%d.txt", path, msg_id) ;
                        if(remove(filepath) == 0){
                            sprintf(msg, "OK Deleted\r\n") ;
                            write_all(fd, msg, strlen(msg)) ;
                            
                            get_timestamp(ts) ;
                            printf("%s User %s DELETE message %d\n", ts, user[client[fd].user_id].name, msg_id) ;
                        }
                        else{
                            sprintf(msg, "ERR No such message\r\n") ;
                            write_all(fd, msg, strlen(msg)) ;
                        }
                    }
                }
            }
            else if(command == 15){
                // Unknown mode
                sprintf(msg, "ERR Unknown mode\r\n") ;
                write_all(fd, msg, strlen(msg)) ;

                char ts[30] ;
                get_timestamp(ts) ;
                printf("%s client on fd %d disconnected (Unknown mode)\n", ts, fd) ;

                close(fd) ;
                FD_CLR(fd, &master_set) ;
                init_client(&client[fd]) ; 
                state[fd] = -2 ;           
            }
            else{
                // Unknown command
                sprintf(msg, "ERR Unknown command\r\n") ;
                write_all(fd, msg, strlen(msg)) ;
            }
        }
    }
    return 0 ;
}