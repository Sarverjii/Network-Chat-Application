#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>  // Added for inet_addr

char ip_address[INET_ADDRSTRLEN];
int portno;
int sockFD;
char username[50];

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void *read_from_server(void *arg) {
    int read_writeFD;
    char buffer[255];

    while (1) {
        bzero(buffer, sizeof(buffer));
        read_writeFD = read(sockFD, buffer, sizeof(buffer) - 1);

        if (read_writeFD <= 0) {
            printf("\033[1;31mServer connection closed.\n\033[0m");
            close(sockFD);
            exit(0);
        }

        if (strcmp(buffer, "\033[1;31mSERVER_SHUTDOWN\n\033[0m") == 0) {
            printf("\033[1;31mServer is shutting down. Closing connection...\n\033[0m");
            close(sockFD);
            exit(0);
        }

        printf("%s", buffer);
    }
}

void *write_to_server(void *arg) {
    int read_writeFD;
    char join_msg[350];
    char buffer[255];

    while (1) {
        bzero(buffer, sizeof(buffer));
        bzero(join_msg, sizeof(join_msg));

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }

        snprintf(join_msg, sizeof(join_msg), "\033[1;33m%s : %s\033[0m", username, buffer);

        read_writeFD = write(sockFD, join_msg, strlen(join_msg));
        if (read_writeFD < 0) {
            break;
        }

        if (strncmp("BYE", buffer, 3) == 0) {
            printf("\033[1;31mYou terminated the connection.\n\033[0m");
            snprintf(join_msg, sizeof(join_msg), "\033[1;31m%s has left the chat\n\033[0m", username);
            write(sockFD, join_msg, strlen(join_msg));
            close(sockFD);
            exit(0);
        }
    }
    return NULL;
}

void askforIPandportno() {
    char temp_ip[INET_ADDRSTRLEN];
    
    while (1) {
        printf("\033[1;32mEnter server IP address: \033[0m");
        if (fgets(temp_ip, sizeof(temp_ip), stdin) == NULL) {
            printf("\033[1;32mError reading input\n\033[0m");
            continue;
        }
        temp_ip[strcspn(temp_ip, "\n")] = '\0';  // Remove newline character
        
        // Validate IP address
        struct in_addr addr;
        if (inet_pton(AF_INET, temp_ip, &addr) == 1) {
            strncpy(ip_address, temp_ip, INET_ADDRSTRLEN);
            break;
        } else {
            printf("\033[1;32mInvalid IP address format. Please try again.\n\033[0m");
        }
    }

    char port_str[10];
    while (1) {
        printf("\033[1;32mEnter server port number: \033[0m");
        if (fgets(port_str, sizeof(port_str), stdin) != NULL) {
            port_str[strcspn(port_str, "\n")] = '\0';
            char *endptr;
            long port = strtol(port_str, &endptr, 10);
            if (*endptr == '\0' && port > 0 && port < 65536) {
                portno = (int)port;
                break;
            }
        }
        printf("\033[1;31mInvalid port number. Please enter a number between 1 and 65535.\n\033[0m");
    }
}

void EstablishConnection() {
    struct sockaddr_in server_addr;

    // Create socket
    sockFD = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFD < 0) {
        error("\033[1;31mError creating socket\033[0m");
    }

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    
    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        error("\033[1;31mInvalid IP address\033[0m");
    }

    // Connect to the server
    if (connect(sockFD, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        error("\033[1;31mError connecting to the server\033[0m");
    }
}

int main() {
    pthread_t read_thread, write_thread;

    // Ask for IP and port number
    askforIPandportno();

    // Establish connection to the server
    EstablishConnection();

    printf("\033[1;32mEnter your Username: \033[0m");
    fgets(username, 50, stdin);
    username[strcspn(username, "\n")] = '\0';

    char buffer[100];
    snprintf(buffer, sizeof(buffer), "\033[1;34m%s joined the chat.\n\033[0m", username);
    write(sockFD, buffer, strlen(buffer));

    printf("\033[1;33m%s You are connected to the server. Type 'BYE' to disconnect.\n\033[0m", username);

    // Create threads for reading and writing
    pthread_create(&read_thread, NULL, read_from_server, NULL);
    pthread_create(&write_thread, NULL, write_to_server, NULL);

    // Wait for both threads to finish
    pthread_join(write_thread, NULL);
    pthread_cancel(read_thread);
    pthread_cancel(write_thread);

    // Close the socket before exiting
    close(sockFD);
    return 0;
}