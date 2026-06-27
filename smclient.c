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

#define maxlen 1024

int write_all(int sockfd, char *buffer, int len){
    int total_sent = 0, total_left = len ;
    while(total_sent < len){
        int n = write(sockfd, buffer+total_sent, total_left) ;
        if(n <= 0){ 
            return n ;
        }
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
        if(n <= 0){
            return n ; 
        }
        buffer[t++] = c ;
        if(c == '\n') break ;
    }
    buffer[t] = '\0' ; 
    return t ;
}

void print_menu(){
    printf("\n1. Send a mail") ;
    printf("\n2. Check my mailbox") ;
    printf("\n3. Quit\n> ") ;
}

void print_menu_(int count, char username[]){
    printf("\nMailbox for %s (%d messages)\n", username, count) ;
    printf("1. List all messages\n") ;
    printf("2. Read a message\n") ;
    printf("3. Delete a message\n") ;
    printf("4. Logout\n> ") ;
}

unsigned long djb2(const char *str) {
    unsigned long hash = 5381 ;
    int c ;
    while ((c = *str++)){
        hash = ((hash << 5) + hash) + c ; /* hash * 33 + c */
    }
    return hash ;
}

int main(int argc, char *argv[]){

    if(argc < 3){
        printf("Error: provide server-ip and port\n./client <server-ip> <port>\n") ;
        exit(1) ;
    }

    char *server_ip = argv[1] ; 
    int server_port = atoi(argv[2]) ;

    printf("Welcome to SimpleMail Client.\n") ;

    while(1){
        print_menu() ;
    
        int option ;
        if (scanf("%d", &option) != 1) {
            printf("Error: invalid input\n") ;
            while (getchar() != '\n') ; 
            continue ;
        }
        
        while (getchar() != '\n') ; 
    
        if(option > 3 || option <= 0){
            printf("Error: must choose an option from the menu\n") ;
            continue ;
        }

        if(option == 3){
            printf("Goodbye.\n") ;
            exit(0) ;
        }
    
        // Establishing fresh connection
        int sockfd = socket(AF_INET, SOCK_STREAM, 0) ;
        if(sockfd < 0){
            perror("Socket failed\n") ;
            exit(1) ;
        }

        struct sockaddr_in server_addr ;
        server_addr.sin_family = AF_INET ;
        server_addr.sin_port = htons(server_port) ;
        inet_pton(AF_INET, server_ip, &(server_addr.sin_addr)) ;

        if(connect(sockfd, (struct sockaddr *)(&server_addr), sizeof(server_addr)) < 0){
            perror("Connect failed\n") ;
            close(sockfd) ;
            continue ; 
        }

        char buffer[1026], msg[1026] ;
        int n = read_all(sockfd, buffer) ; 
        if(n <= 0){
            printf("Server disconnected immediately.\n") ;
            close(sockfd) ;
            continue ;
        }

        // Option 1: Sending Mail (SMTP2)
        if(option == 1){

            sprintf(msg, "MODE SEND\r\n") ;
            write_all(sockfd, msg, strlen(msg)) ;
            read_all(sockfd, buffer) ;
            
            if(strcmp(buffer, "OK\r\n")){
                printf("Error initializing MODE SEND.\n") ;
                close(sockfd) ;
                continue ;
            }
    
            printf("From (your name): ") ;
            char name[514] ;
            fgets(name, 512, stdin) ;
            name[strcspn(name, "\r\n")] = '\0' ; 
    
            sprintf(msg, "FROM %s\r\n", name) ;
            write_all(sockfd, msg, strlen(msg)) ;
            read_all(sockfd, buffer) ;
    
            if(strcmp(buffer, "OK Sender accepted\r\n")){
                printf("Error: Sender rejected by server.\n") ;
                close(sockfd) ;
                continue ;
            }
    
            int count = 0 ; 
    
            // Getting the names of recipients
            while(1){
                printf("To (recipient username, empty line to finish): ") ;
                fgets(name, 512, stdin) ;
                name[strcspn(name, "\r\n")] = '\0' ; 
    
                if(strlen(name) == 0) break ;
    
                sprintf(msg, "TO %s\r\n", name) ;
                write_all(sockfd, msg, strlen(msg)) ;
                read_all(sockfd, buffer) ;

                if(!strcmp(buffer, "ERR No such user\r\n")){
                    printf("\t-> Error: user '%s' does not exist on this server.\n", name) ;
                    continue ;
                }
                else if(!strcmp(buffer, "OK Recipient accepted\r\n")){
                    printf("\t-> Recipient '%s' accepted.\n", name) ;
                    count++ ;
                }
                else {
                    printf("Unexpected server response: %s", buffer) ;
                    break ;
                }
            }

            // If no registered users were chosen by user
            if(count == 0){
                printf("Error: No valid recipients provided. Aborting mail.\n") ;
                sprintf(msg, "QUIT\r\n") ;
                write_all(sockfd, msg, strlen(msg)) ;
                read_all(sockfd, buffer) ;
                close(sockfd) ;
                continue ;
            }
    
            char sub[514] ;
            printf("Subject: ") ;
            fgets(sub, 512, stdin) ;
            sub[strcspn(sub, "\r\n")] = '\0' ; 
    
            sprintf(msg, "SUB %s\r\n", sub) ;
            write_all(sockfd, msg, strlen(msg)) ;
            read_all(sockfd, buffer) ;
    
            if(strcmp(buffer, "OK Subject accepted\r\n")){
                printf("Error accepting subject.\n") ;
                close(sockfd) ;
                continue ;
            }
    
            sprintf(msg, "BODY\r\n") ;
            write_all(sockfd, msg, strlen(msg)) ;
            read_all(sockfd, buffer) ;
            
            if(strcmp(buffer, "OK Send body, end with CRLF.CRLF\r\n")){
                printf("Error: Server refused BODY command.\n") ;
                close(sockfd) ;
                continue ;
            }
    
            printf("Body (type '.' on a line by itself to finish):\n") ;
            while(1){
                char line[514] ;
                fgets(line, 512, stdin) ;
                line[strcspn(line, "\r\n")] = '\0' ; 
                
                if(!strcmp(line, ".")){
                    sprintf(msg, ".\r\n") ;
                    write_all(sockfd, msg, strlen(msg)) ;
                    break ;
                }

                if(line[0] == '.'){
                    // byte stuffing
                    sprintf(msg, ".%s\r\n", line) ;
                }
                else{
                    sprintf(msg, "%s\r\n", line) ;
                }
    
                write_all(sockfd, msg, strlen(msg)) ;
            }

            read_all(sockfd, buffer) ;
            
            if(strncmp(buffer, "OK Delivered", 12) == 0){
                printf("\nMail delivered successfully to %d recipient(s).\n", count) ;
            } else {
                printf("Delivery failed: %s\n", buffer) ;
            }

            sprintf(msg, "QUIT\r\n") ;
            write_all(sockfd, msg, strlen(msg)) ;
            read_all(sockfd, buffer) ; 
            
            close(sockfd) ; 
        }

        // Option 2: Checking mailbox (SMP)
        else if (option == 2){

            sprintf(msg, "MODE RECV\r\n") ;
            write_all(sockfd, msg, strlen(msg)) ;
            int n = read_all(sockfd, buffer) ;
            if(n <= 0 || strcmp(buffer, "OK\r\n") != 0){
                printf("Error initializing MODE RECV.\n") ;
                close(sockfd) ;
                continue ;
            }

            char nonce[9] ;
            n = read_all(sockfd, buffer) ;
            if(n <= 0){
                if(n < 0) perror("read") ;
                printf("Server disconnected\n") ;
                close(sockfd) ;
                continue ;
            }
            if(strncmp(buffer, "AUTH REQUIRED ", 14)){
                printf("Error: bad server response\n") ;
                close(sockfd) ;
                continue ;
            }

            // Getting the nonce given by server
            strncpy(nonce, buffer+14, 8) ;
            nonce[8] = '\0' ;
            
            char username[21], password[31] ;
            int q = 0, s = 0 ;
            while(!q && !s){
                // prompting user to enter his username and password
                printf("Username: ") ;
                scanf("%20s", username) ;
    
                printf("Password: ") ;
                scanf("%30s", password) ;
                while (getchar() != '\n') ; 

                // computing hash
                char str[100] ;
                sprintf(str, "%s%s", password, nonce) ;
                unsigned long hash = djb2(str) ;

                // sending the computed hash to server
                sprintf(msg, "AUTH %s %lu\r\n", username, hash) ;
                write_all(sockfd, msg, strlen(msg)) ;

                int n = read_all(sockfd, buffer) ;
                if(n <= 0){
                    if(n < 0) perror("read") ;
                    printf("Server disconnected\n") ;
                    close(sockfd) ;
                    q = 1 ;
                    break ;
                }

                if(!strcmp(buffer, "ERR Authentication failed\r\n")){
                    printf("Error: username doesn't exist or password is incorrect, try again...\n") ;
                    continue ;
                }

                // If the server's trial limit has exceeded, we close the connection
                if(!strcmp(buffer, "ERR Too many failures\r\n")){
                    printf("Too many unsuccessful attempts, closing MODE RECV\n") ;
                    q = 1 ;
                    break ;
                }

                char temp[200] ;
                sprintf(temp, "OK Welcome %s\r\n", username) ;
                if(!strcmp(buffer, temp)){
                    printf("\nWelcome, %s!\n", username) ;
                    q = 1, s = 1 ;
                    break ;
                }
            }

            if(!s){
                close(sockfd) ;
                continue ;
            }

            while(1){
                sprintf(msg, "COUNT\r\n") ;
                write_all(sockfd, msg, strlen(msg)) ;

                int n = read_all(sockfd, buffer) ;
                if(n <= 0){
                    if(n < 0) perror("read") ;
                    printf("Server disconnected\n") ;
                    close(sockfd) ;
                    break ;
                }

                if(strncmp(buffer, "OK ", 3)){
                    printf("Error: bad server response\n") ;
                    close(sockfd) ;
                    break ;
                }

                int count = atoi(buffer+3) ;
                print_menu_(count, username) ;
                
                int opt2 ;
                if(scanf("%d", &opt2) != 1) {
                    while(getchar() != '\n') ;
                    continue ;
                }
                while(getchar() != '\n') ; 

                if(opt2 == 1){
                    // LIST
                    sprintf(msg, "LIST\r\n") ;
                    write_all(sockfd, msg, strlen(msg)) ;
                    read_all(sockfd, buffer) ;
                    
                    if (strncmp(buffer, "OK ", 3) == 0) {
                        printf("\n%-5s | %-20s | %-30s | %-20s\n", "ID", "From", "Subject", "Date") ;
                        printf("%-5s   %-20s   %-30s   %-20s\n", "--", "----", "-------", "----") ;
                        
                        while(1) {
                            read_all(sockfd, buffer) ;
                            buffer[strcspn(buffer, "\r\n")] = '\0' ;
                            if (strcmp(buffer, ".") == 0) break ;
                            
                            char *id = buffer ;
                            char *from = strchr(id, '\t') ; 
                            if(from) *from++ = '\0' ; else from = "" ;
                            
                            char *sub = strchr(from, '\t') ; 
                            if(sub) *sub++ = '\0' ; else sub = "" ;
                            
                            char *date = strchr(sub, '\t') ; 
                            if(date) *date++ = '\0' ; else date = "" ;
                            
                            printf("%-5s | %-20s | %-30s | %-20s\n", id, from, sub, date) ;
                        }
                    }
                } 
                else if(opt2 == 2){
                    // READ
                    int msg_id ;
                    printf("Enter message ID: ") ;
                    scanf("%d", &msg_id) ;
                    while(getchar() != '\n') ;
                    
                    sprintf(msg, "READ %d\r\n", msg_id) ;
                    write_all(sockfd, msg, strlen(msg)) ;
                    read_all(sockfd, buffer) ;
                    
                    if(strcmp(buffer, "OK\r\n") == 0){
                        printf("\n") ;
                        while(1){
                            read_all(sockfd, buffer) ;
                            buffer[strcspn(buffer, "\r\n")] = '\0' ;
                            
                            if(strcmp(buffer, ".") == 0) break ;
                            
                            if(buffer[0] == '.'){
                                printf("%s\n", buffer + 1) ;
                            } else {
                                printf("%s\n", buffer) ;
                            }
                        }
                    }
                    else{
                        printf("%s", buffer) ;
                    }
                }
                else if(opt2 == 3){
                    // DELETE
                    int msg_id ;
                    printf("Enter message ID: ") ;
                    scanf("%d", &msg_id) ;
                    while(getchar() != '\n') ;
                    
                    sprintf(msg, "DELETE %d\r\n", msg_id) ;
                    write_all(sockfd, msg, strlen(msg)) ;
                    read_all(sockfd, buffer) ;
                    
                    if(strcmp(buffer, "OK Deleted\r\n") == 0){
                        printf("Message %d deleted.\n", msg_id) ;
                    }
                    else{
                        printf("%s", buffer) ;
                    }
                }
                else if(opt2 == 4){
                    // LOGOUT
                    sprintf(msg, "QUIT\r\n") ;
                    write_all(sockfd, msg, strlen(msg)) ;
                    read_all(sockfd, buffer) ;
                    printf("Logged out.\n") ;
                    close(sockfd) ;
                    break ;
                }
                else {
                    printf("Error: invalid option.\n") ;
                }
            }
        }
    }
    return 0 ;
}