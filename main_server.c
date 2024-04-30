#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT_NUM 1004

void error(const char *msg)
{
	perror(msg);
	exit(1);
}

typedef struct _USR {
	int clisockfd;		// socket file descriptor
    char* ipaddr;       //ip address
    int color;
	struct _USR* next;	// for linked list queue
} USR;

USR *head = NULL;
USR *tail = NULL;

void add_tail(int newclisockfd, char* addr, int color)
{
	if (head == NULL) {
		head = (USR*) malloc(sizeof(USR));
		head->clisockfd = newclisockfd;
        head->ipaddr = addr;
        head->color = color;
		head->next = NULL;
		tail = head;
	} else {
		tail->next = (USR*) malloc(sizeof(USR));
		tail->next->clisockfd = newclisockfd;
        tail->next->ipaddr = addr;
        tail->next->color = color;
		tail->next->next = NULL;
		tail = tail->next;
	}
}

int deleteClient(int sockfd) {
    USR* cur = head;
    USR* prev;
    if(cur != NULL && cur->clisockfd == sockfd) {
        head = cur->next;
        printf("%s disconnected from the server\n", cur->ipaddr);
        free(cur);
        return 1;
    }
    while(cur != NULL && cur->clisockfd != sockfd) {
        prev = cur;
        cur = cur->next;
    }
    if(cur == NULL) return 0;
    prev->next = cur->next;
    printf("%s disconnected from the server\n", cur->ipaddr);
    free(cur);
    return 1;
}

void printClients() {
    USR* cur = head;
    printf("Connected Clients: \n");
    while(cur != NULL) {
        printf("    %s\n", cur->ipaddr);
        cur = cur->next;
    }
}

void broadcast(int fromfd, char* message)
{
	// figure out sender address
	struct sockaddr_in cliaddr;
	socklen_t clen = sizeof(cliaddr);
	if (getpeername(fromfd, (struct sockaddr*)&cliaddr, &clen) < 0) error("ERROR Unknown sender!");

	// traverse through all connected clients
	USR* cur = head;
	while (cur != NULL) {
		// check if cur is not the one who sent the message
		if (cur->clisockfd != fromfd) {
			char buffer[512];

			// prepare message
            memset(buffer, 0, 256);
			sprintf(buffer, "\x1b[%dm[%s]:%s\x1b[0m", cur->color, cur->ipaddr, message);
			int nmsg = strlen(buffer);

			// send!
			int nsen = send(cur->clisockfd, buffer, nmsg, 0);
			if (nsen != nmsg) error("ERROR send() failed");
		}

		cur = cur->next;
	}
}

typedef struct _ThreadArgs {
	int clisockfd;
} ThreadArgs;

void* thread_main(void* args)
{
	// make sure thread resources are deallocated upon return
	pthread_detach(pthread_self());

	// get socket descriptor from argument
	int clisockfd = ((ThreadArgs*) args)->clisockfd;
	free(args);

	//-------------------------------
	// Now, we receive/send messages
	char buffer[256];
	int nsen, nrcv;

	nrcv = recv(clisockfd, buffer, 255, 0);
	if (nrcv < 0) error("ERROR recv() failed");
	while (nrcv > 0) {
		// we send the message to everyone except the sender
		broadcast(clisockfd, buffer);

        memset(buffer, 0, 256);
		nrcv = recv(clisockfd, buffer, 255, 0);
		if (nrcv < 0) error("ERROR recv() failed");
        //if(buffer[0] == '\0') break;
	}

    if(deleteClient(clisockfd) == 0) {
        printf("could not disconnect that socket\n");
    }
    printClients();
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
	//serv_addr.sin_addr.s_addr = inet_addr("192.168.1.171");	
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
		add_tail(newsockfd, inet_ntoa(cli_addr.sin_addr), (rand() % 8) + 30); // add this new client to the client list
        printClients();

		// prepare ThreadArgs structure to pass client socket
		ThreadArgs* args = (ThreadArgs*) malloc(sizeof(ThreadArgs));
		if (args == NULL) error("ERROR creating thread argument");
		
		args->clisockfd = newsockfd;

		pthread_t tid;
		if (pthread_create(&tid, NULL, thread_main, (void*) args) != 0) error("ERROR creating a new thread");
	}

	return 0; 
}

