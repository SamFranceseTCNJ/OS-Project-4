#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <pthread.h>

#define PORT_NUM 4000

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

typedef struct _ThreadArgs {
    int clisockfd;
} ThreadArgs;

void* thread_main_recv(void* args)
{
    pthread_detach(pthread_self());

    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    // keep receiving and displaying message from server
    char buffer[512];
    int n;

    do {
        memset(buffer, 0, 512);
        n = recv(sockfd, buffer, 512, 0);
        if (n < 0) error("ERROR recv() failed");
        
        printf("%s\n", buffer);
    } while(n > 0);
    
    return NULL;
}

void* thread_main_send(void* args)
{
    pthread_detach(pthread_self());

    int sockfd = ((ThreadArgs*) args)->clisockfd;
    free(args);

    // keep sending messages to the server
    char buffer[256];
    int n;

    while (1) {
        // You will need a bit of control on your terminal
        // console or GUI to have a nice input window.
        printf("Please enter the message: ");
        memset(buffer, 0, 256);
        fgets(buffer, 255, stdin);

        if (strlen(buffer) == 1) buffer[0] = '\0';

        n = send(sockfd, buffer, strlen(buffer), 0);
        if (n < 0) error("ERROR writing to socket");
        
        if (n == 0) break; // we stop transmission when user type empty string

        // Check if the message is a file transfer request
        if (strncmp(buffer, "SEND", 4) == 0) {
            char receiving_username[20];
            char filename[256];

            // Parsing the SEND command
            sscanf(buffer, "SEND %s %s", receiving_username, filename);

            // Wait for the receiver's response
            printf("User %s wants to send you a file named '%s'. Do you accept? (Y/N) ", receiving_username, filename);
            memset(buffer, 0, 256);
            fgets(buffer, 255, stdin);
            if (buffer[0] == 'Y' || buffer[0] == 'y') {
                // Notify the server to start the file transfer
                n = send(sockfd, "Y", 1, 0);
                if (n < 0) error("ERROR writing to socket");

                // Receive the file transfer information from the server
                memset(buffer, 0, 256);
                n = recv(sockfd, buffer, 255, 0);
                if (n < 0) error("ERROR recv() failed");

                // Parse the file transfer information
                char file_name[256];
                long file_size;
                sscanf(buffer, "FILE %s %ld", file_name, &file_size);

                // Open the file to write
                FILE *file = fopen(file_name, "w");
                if (file == NULL) {
                    printf("Error opening file.\n");
                    continue;
                }

                // Receive the file data from the server
                long total_bytes_received = 0;
                while (total_bytes_received < file_size) {
                    memset(buffer, 0, 256);
                    n = recv(sockfd, buffer, 256, 0);
                    if (n < 0) error("ERROR recv() failed");

                    fwrite(buffer, 1, n, file);
                    total_bytes_received += n;
                }

                printf("File '%s' received successfully.\n", file_name);
                fclose(file);
            } else {
                // Notify the server that the receiver rejected the file
                n = send(sockfd, "N", 1, 0);
                if (n < 0) error("ERROR writing to socket");
            }
        }
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2) error("Please specify hostname and room number or 'new' to create a new room");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    struct sockaddr_in serv_addr;
    socklen_t slen = sizeof(serv_addr);
    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(PORT_NUM);

    printf("Try connecting to %s...\n", inet_ntoa(serv_addr.sin_addr));

    int status = connect(sockfd, 
            (struct sockaddr *) &serv_addr, slen);
    if (status < 0) error("ERROR connecting");

    pthread_t tid1;
    pthread_t tid2;

    ThreadArgs* args;

    if (argc == 2 || (argc > 2 && strncmp(argv[2], "new", 3) != 0)) {
        // Send a request to get the list of available rooms
        char buffer[256] = "list";
        send(sockfd, buffer, sizeof(buffer), 0);

        // Receive and print the list of available rooms
        memset(buffer, 0, 256);
        recv(sockfd, buffer, sizeof(buffer), 0);
        printf("%s", buffer);

        // Choose a room to join
        printf("Choose the room number or type [new] to create a new room: ");
        fgets(buffer, 255, stdin);
        buffer[strlen(buffer) - 1] = '\0'; // remove newline character

        // Send the chosen room number or new room command to the server
        send(sockfd, buffer, strlen(buffer), 0);
    } else {
        // Send the room number or new room command to the server
        send(sockfd, argv[2], strlen(argv[2]), 0);
    }

    // send user name to server
    char username[20];
    printf("Type your user name: ");
    fgets(username, 20, stdin);
    username[strlen(username) - 1] = '\0'; // removing newline character
    send(sockfd, username, sizeof(username), 0);

    args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
    args->clisockfd = sockfd;
    pthread_create(&tid1, NULL, thread_main_send, (void*) args);

    args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
    args->clisockfd = sockfd;
    pthread_create(&tid2, NULL, thread_main_recv, (void*) args);

    // parent will wait for sender to finish (= user stop sending message and disconnect from server)
    pthread_join(tid2, NULL);

    close(sockfd);

    return 0;
}




