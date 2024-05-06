#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h> 
#include <fcntl.h>  

#define PORT_NUM 4000
#define MAX_ROOMS 3
#define MAX_CLIENTS 10

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

typedef struct _USR {
    int clisockfd;      // socket file descriptor
    char* ipaddr;       // ip address
    int color;
    char username[20];  // user name
    struct _USR* next;  // for linked list queue
} USR;

typedef struct _Room {
    int room_number;
    USR* head;
    USR* tail;
    struct _Room* next;
} Room;

Room *rooms[MAX_ROOMS] = {NULL};

int find_room(int room_number) {
    for (int i = 0; i < MAX_ROOMS; ++i) {
        if (rooms[i] != NULL && rooms[i]->room_number == room_number)
            return i;
    }
    return -1;
}

void add_tail(Room* room, int newclisockfd, char* addr, int color, char* username)
{
    if (room->head == NULL) {
        room->head = (USR*) malloc(sizeof(USR));
        room->head->clisockfd = newclisockfd;
        room->head->ipaddr = addr;
        room->head->color = color;
        strcpy(room->head->username, username);
        room->head->next = NULL;
        room->tail = room->head;
    } else {
        room->tail->next = (USR*) malloc(sizeof(USR));
        room->tail->next->clisockfd = newclisockfd;
        room->tail->next->ipaddr = addr;
        room->tail->next->color = color;
        strcpy(room->tail->next->username, username);
        room->tail->next->next = NULL;
        room->tail = room->tail->next;
    }
}

int deleteClient(Room* room, int sockfd) {
    USR* cur = room->head;
    USR* prev;
    if(cur != NULL && cur->clisockfd == sockfd) {
        room->head = cur->next;
        printf("%s disconnected from the server\n", cur->username);
        free(cur);
        return 1;
    }
    while(cur != NULL && cur->clisockfd != sockfd) {
        prev = cur;
        cur = cur->next;
    }
    if(cur == NULL) return 0;
    prev->next = cur->next;
    printf("%s disconnected from the server\n", cur->username);
    free(cur);
    return 1;
}

void printClients(Room* room) {
    USR* cur = room->head;
    printf("Connected Clients in Room %d: \n", room->room_number);
    while(cur != NULL) {
        printf("    %s\n", cur->username);
        cur = cur->next;
    }
}

void broadcast(Room* room, int fromfd, char* username, char* message)
{
    // traverse through all connected clients
    USR* cur = room->head;
    while (cur != NULL) {
        // check if cur is not the one who sent the message
        if (cur->clisockfd != fromfd) {
            char buffer[512];

            // prepare message
            memset(buffer, 0, 512);
            sprintf(buffer, "\x1b[%dm[Room %d][%s]: %s\x1b[0m", cur->color, room->room_number, username, message);
            int nmsg = sizeof(buffer);

            // send!
            int nsen = send(cur->clisockfd, buffer, nmsg, 0);
            if (nsen != nmsg) error("ERROR send() failed");
        }

        cur = cur->next;
    }
}

typedef struct _ThreadArgs {
    int clisockfd;
    int room_number;
    char* addr;
} ThreadArgs;

void* thread_main(void* args)
{
    // make sure thread resources are deallocated upon return
    pthread_detach(pthread_self());

    // get socket descriptor from argument
    ThreadArgs* targs = (ThreadArgs*) args;
    int clisockfd = targs->clisockfd;
    int room_number = targs->room_number;
    char* ip = targs->addr;
    free(args);

    if (find_room(room_number) == -1) {
        printf("Room %d not found\n", room_number);
        close(clisockfd);
        return NULL;
    }

    //-------------------------------
    // Now, we receive/send messages
    char buffer[512];
    int nsen, nrcv;

    // receive user name from client
    char username[20];
    nrcv = recv(clisockfd, username, sizeof(username), 0);
    if (nrcv < 0) error("ERROR recv() failed");
    username[nrcv] = '\0';

    //add tail
    Room* room = rooms[find_room(room_number)];
    add_tail(room, clisockfd, ip, (rand() % 8) + 30, username);

    // we send the user name to everyone
    printf("%s (%s) joined Room %d!\n", username, ip, room_number);
    printClients(room);
    broadcast(room, clisockfd, username, "joined the chat room!");

    while (1) {
        // receive message from client
        memset(buffer, 0, 512);
        nrcv = recv(clisockfd, buffer, 511, 0);
        if (nrcv < 0) error("ERROR recv() failed");

        if (nrcv == 0) break; // client disconnected

        // Check if the message is a file transfer request
        if (strncmp(buffer, "SEND", 4) == 0) {
            char receiving_username[20];
            char filename[256];

            // Parsing the SEND command
            sscanf(buffer, "SEND %s %s", receiving_username, filename);

            // Ask the receiving client if they accept the file
            sprintf(buffer, "User %s wants to send you a file named '%s'. Do you accept? (Y/N) ", username, filename);
            nsen = send(clisockfd, buffer, strlen(buffer), 0);
            if (nsen != strlen(buffer)) error("ERROR send() failed");

            // Receive response from the receiving client
            memset(buffer, 0, 512);
            nrcv = recv(clisockfd, buffer, 511, 0);
            if (nrcv < 0) error("ERROR recv() failed");

            // If the receiving client accepts, start the file transfer
            if (buffer[0] == 'Y' || buffer[0] == 'y') {
                // Open the file to be sent
                FILE* file = fopen(filename, "r");
                if (file == NULL) {
                    printf("Error opening file.\n");
                    continue;
                }

                // Get the size of the file
                fseek(file, 0, SEEK_END);
                long filesize = ftell(file);
                rewind(file);

                // Prepare the file transfer message
                memset(buffer, 0, 512);
                sprintf(buffer, "FILE %s %ld", filename, filesize);
                nsen = send(clisockfd, buffer, strlen(buffer), 0);
                if (nsen != strlen(buffer)) error("ERROR send() failed");

                // Send the file in chunks
                while (filesize > 0) {
                    int bytes_to_read = (filesize < 512) ? filesize : 512;
                    fread(buffer, 1, bytes_to_read, file);
                    nsen = send(clisockfd, buffer, bytes_to_read, 0);
                    if (nsen != bytes_to_read) error("ERROR send() failed");
                    filesize -= bytes_to_read;
                }

                fclose(file);

                // Notify the sender that the file has been sent
                sprintf(buffer, "File '%s' has been sent to %s.", filename, receiving_username);
                nsen = send(clisockfd, buffer, strlen(buffer), 0);
                if (nsen != strlen(buffer)) error("ERROR send() failed");
            }
        } else {
            // we send the message to everyone except the sender
            broadcast(room, clisockfd, username, buffer);
        }
    }

    if (deleteClient(room, clisockfd) == 0) {
        printf("could not disconnect that socket\n");
    }
    printClients(room);
    close(clisockfd);
    //-------------------------------

    return NULL;
}




int main(int argc, char *argv[])
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    struct sockaddr_in serv_addr;
    socklen_t slen = sizeof(serv_addr);
    memset((char*) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;    
    serv_addr.sin_port = htons(PORT_NUM);

    int status = bind(sockfd, 
            (struct sockaddr*) &serv_addr, slen);
    if (status < 0) error("ERROR on binding");

    listen(sockfd, 5); // maximum number of connections = 5

    while(1) {
        struct sockaddr_in cli_addr;
        socklen_t clen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, 
            (struct sockaddr *) &cli_addr, &clen);
        if (newsockfd < 0) error("ERROR on accept");

        printf("Connected: %s\n", inet_ntoa(cli_addr.sin_addr));

        char buffer[256];
        int n = recv(newsockfd, buffer, 255, 0);
        if (n < 0) error("ERROR recv() failed");
        int room_number;
        if (strncmp(buffer, "new", 3) == 0) {
            room_number = -1;
            for (int i = 0; i < MAX_ROOMS; ++i) {
                if (rooms[i] == NULL) {
                    room_number = i + 1;
                    rooms[i] = (Room*) malloc(sizeof(Room));
                    rooms[i]->room_number = room_number;
                    rooms[i]->head = NULL;
                    rooms[i]->tail = NULL;
                    rooms[i]->next = NULL;
                    break;
                }
            }
            if (room_number == -1) {
                send(newsockfd, "Maximum number of rooms reached!", sizeof("Maximum number of rooms reached!"), 0);
                close(newsockfd);
                continue;
            }
            printf("New room created: %d\n", room_number);
            sprintf(buffer, "Connected to %s with new room number %d\nPlease enter the message: ", inet_ntoa(cli_addr.sin_addr), room_number);
            send(newsockfd, buffer, sizeof(buffer), 0);
        } else if (strncmp(buffer, "list", 4) == 0) {
            // Send the list of available rooms
            memset(buffer, 0, 256);
            sprintf(buffer, "Server says following options are available:\n");
            for (int i = 0; i < MAX_ROOMS; ++i) {
                if (rooms[i] == NULL) {
                    sprintf(buffer + strlen(buffer), "Room %d: 0 people\n", i + 1);
                } else {
                    int count = 0;
                    USR *cur = rooms[i]->head;
                    while (cur != NULL) {
                        count++;
                        cur = cur->next;
                    }
                    sprintf(buffer + strlen(buffer), "Room %d: %d people\n", rooms[i]->room_number, count);
                }
            }
            sprintf(buffer + strlen(buffer), "Choose the room number or type [new] to create a new room: ");
            send(newsockfd, buffer, sizeof(buffer), 0);
            close(newsockfd);
            continue;
        } else {
            room_number = atoi(buffer);
            Room* room = rooms[find_room(room_number)];
            if (room == NULL) {
                send(newsockfd, "Room does not exist!", sizeof("Room does not exist!"), 0);
                close(newsockfd);
                continue;
            }
            printf("Client joined room %d\n", room_number);
            sprintf(buffer, "Connected to %s in room %d\nPlease enter the message: ", inet_ntoa(cli_addr.sin_addr), room_number);
            send(newsockfd, buffer, sizeof(buffer), 0);
        }

        // prepare ThreadArgs structure to pass client socket
        ThreadArgs* args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
        if (args == NULL) error("ERROR creating thread argument");
        
        args->clisockfd = newsockfd;
        args->room_number = room_number;
        args->addr = inet_ntoa(cli_addr.sin_addr);

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_main, (void*) args) != 0) error("ERROR creating a new thread");
    }

    return 0; 
}



