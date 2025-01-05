#include <arpa/inet.h>
#include <ifaddrs.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 255
int sockFD, portno, numberofConnections;
char ip_address[INET_ADDRSTRLEN]; 
int acceptedSockets[10];
int acceptedSocketcount = 0;
volatile int server_running = 1;
pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;

void getportnoandIP() {
    struct ifaddrs *ifaddr, *ifa;

    // Get a list of network interfaces
    if (getifaddrs(&ifaddr) == -1) {
        perror("Error getting network interfaces");
        exit(1);
    }

    // Iterate through the interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) { // Check for IPv4
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;

            // Ignore the loopback interface
            if (strcmp(ifa->ifa_name, "lo") != 0) {
                inet_ntop(AF_INET, &sa->sin_addr, ip_address, INET_ADDRSTRLEN);
                printf("The server is running on IP Address : %s \n",ip_address);
                //printf("Interface: %s, IP Address: %s\n", ifa->ifa_name, ip_address);
                break;
            }
        }
    }

    freeifaddrs(ifaddr);

    if (strlen(ip_address) == 0) {
        fprintf(stderr, "No active network interface found.\n");
        exit(1);
    }

    // Randomly select a port between 20000 and 30000
    srand(time(NULL));
    int is_port_free = 0;
    while (!is_port_free) {
        portno = rand() % 10001 + 20000; // Generates a random port between 20000-30000

        // Create a temporary socket to check if the port is free
        int temp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (temp_sock < 0) {
            perror("Error creating temporary socket");
            exit(1);
        }

        struct sockaddr_in temp_addr;
        temp_addr.sin_family = AF_INET;
        temp_addr.sin_port = htons(portno);
        temp_addr.sin_addr.s_addr = INADDR_ANY;

        // Try binding to the port
        if (bind(temp_sock, (struct sockaddr *)&temp_addr, sizeof(temp_addr)) == 0) {
            is_port_free = 1; // Port is free
        }

        close(temp_sock);
    }

    printf("Selected Port Number: %d\n", portno);

}

void creatingAndBindingSocket() {
    struct sockaddr_in server_addr;
    sockFD = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFD < 0) {
        perror("Error making socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockFD, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        exit(1);
    }

}

void numberOfConnections() {
    char input[BUFFER_SIZE];
    while (1) {
        printf("Enter the maximum number of people that can join the chat: ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            // Validate and convert input to an integer
            char *endptr;
            numberofConnections = strtol(input, &endptr, 10);
            
            // Check if the input is valid and greater than or equal to 2
            if (endptr != input && *endptr == '\n' && numberofConnections >= 2) {
                printf("The number of people that can join the chat is: %d\n", numberofConnections);
                break;
            }
        }
        printf("Invalid input. Please enter a valid number greater than or equal to 2.\n");
    }
}


// Error-handling function
void error(const char *msg) {
    perror(msg);
    exit(1);
}

void sendtoallotherusers(char buffer[255], int newsockFD){
    for(int i =0; i<acceptedSocketcount; i++)
    {
        if(acceptedSockets[i] != newsockFD)
        {
            int writeFD = write(acceptedSockets[i], buffer, strlen(buffer));
            if (writeFD < 0) {
                error("Error writing to socket");
            }   
        }
    }
}


// Updated Reading function
void *Reading(void *arg) {
    int newsockFD = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (server_running) {
        bzero(buffer, BUFFER_SIZE);
        int readFD = read(newsockFD, buffer, BUFFER_SIZE - 1);

        if (readFD <= 0) {
            pthread_mutex_lock(&socket_mutex);
            // Remove the socket from the acceptedSockets array
            for (int i = 0; i < acceptedSocketcount; i++) {
                if (acceptedSockets[i] == newsockFD) {
                    acceptedSockets[i] = acceptedSockets[acceptedSocketcount - 1];
                    acceptedSocketcount--;
                    break;
                }
            }
            close(newsockFD);
            pthread_mutex_unlock(&socket_mutex);
            break;
        }

        printf("%s", buffer);
        sendtoallotherusers(buffer, newsockFD);
    }
    return NULL;
}


void cleanup_and_exit() {
    // Set the flag to stop all threads
    server_running = 0;

    char buffer[255];
    snprintf(buffer, sizeof(buffer), "SERVER_SHUTDOWN\n");
    
    pthread_mutex_lock(&socket_mutex);
    // Notify all clients
    for (int i = 0; i < acceptedSocketcount; i++) {
        if (acceptedSockets[i] > 0) {
            write(acceptedSockets[i], buffer, strlen(buffer));
            close(acceptedSockets[i]);
            acceptedSockets[i] = -1;
        }
    }
    
    // Close server socket
    if (sockFD > 0) {
        close(sockFD);
        sockFD = -1;
    }
    pthread_mutex_unlock(&socket_mutex);
    
    printf("Server shut down successfully.\n");
    exit(0);
}

void *Writing(void *arg) {
    char buffer[255];
    while(server_running) {
        bzero(buffer, sizeof(buffer));
        if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            if (strncmp("BYE", buffer, 3) == 0) {
                printf("Initiating server shutdown...\n");
                cleanup_and_exit();
                break;
            }
        }
    }
    return NULL;
}

// Updated ReciveandPrintData function
void *ReciveandPrintData(void *arg) {
    int newsockFD = *(int *)arg; // Cast void * to int *
    pthread_t readThread, writeThread;

    if (pthread_create(&readThread, NULL, Reading, &newsockFD) != 0) {
        error("Error creating read thread");
    }
    if (pthread_create(&writeThread, NULL, Writing, &newsockFD) != 0) {
        error("Error creating read thread");
    }
    

    pthread_join(readThread, NULL); // Wait for threads to complete

    return NULL;
}

// Updated CreateThreadforAcceptedConnection function
void CreateThreadforAcceptedConnection(int newsockFD) {
    pthread_t id;
    int *sockPtr = malloc(sizeof(int)); // Allocate memory for newsockFD
    if (sockPtr == NULL) {
        error("Memory allocation failed");
    }
    *sockPtr = newsockFD; // Store the value of newsockFD in dynamically allocated memory

    if (pthread_create(&id, NULL, ReciveandPrintData, sockPtr) != 0) {
        error("Error creating connection thread");
    }
}


void AcceptingNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_length = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    
    while(server_running) {
        int newsockFD = accept(sockFD, (struct sockaddr *)&client_addr, &client_length);
        if (newsockFD < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                usleep(100000);  // 100ms
                continue;
            }
            if (!server_running) break;
            error("Error while accepting connection");
        }

        pthread_mutex_lock(&socket_mutex);
        if (acceptedSocketcount < numberofConnections) {
            acceptedSockets[acceptedSocketcount++] = newsockFD;
        } else {
            close(newsockFD);  // Reject connections if the server is full
        }
        pthread_mutex_unlock(&socket_mutex);
        
        bzero(buffer, BUFFER_SIZE);
        read(newsockFD, buffer, BUFFER_SIZE - 1);
        sendtoallotherusers(buffer, newsockFD);
        printf("%s", buffer);
        CreateThreadforAcceptedConnection(newsockFD);
    }
}





int main() {
    getportnoandIP();
    creatingAndBindingSocket();
    numberOfConnections();
    listen(sockFD, numberofConnections);

      //Accepting Connection
    AcceptingNewConnection();
   
    // Close the sockets
    close(sockFD);

   
    return 0;
}
