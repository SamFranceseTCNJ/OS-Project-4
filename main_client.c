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

#define PORT_NUM 4444

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
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 3) error("Please specify hostname and room number or 'new' to create a new room");

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

    if (strcmp(argv[2], "new") == 0) {
        send(sockfd, "new", sizeof("new"), 0);
        char buffer[256];
        recv(sockfd, buffer, sizeof(buffer), 0);
        printf("%s", buffer);
    } else {
        int room_number = atoi(argv[2]);
        send(sockfd, argv[2], strlen(argv[2]), 0);
        char buffer[256];
        recv(sockfd, buffer, sizeof(buffer), 0);
        printf("%s", buffer);
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




