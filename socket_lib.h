#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SOCK_LOCAL_ADDR "./socket_420"  //Criar name com pid()? Se sim então têm de ser em /tmp e temos de garantir unlink

/*
#include <sys/types.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
 */

#define MAX_STR_SIZE 200

struct metaData{
    int action;         //0->client wants to send data to server. 1->client is requesting data from server
    int region;
    size_t msg_size;
};


int createSocket(int domain, int type);
int UnixServerSocket(int sockId, char * path, int maxQueueLength);
int InternetServerSocket(int sockId, int port,int maxQueueLength);
int UnixClientSocket(int sockId , char * path);
int InternetClientSocket(int sockId, char *ip, int port);
int handShake(int sockId, char * info, size_t size);
char * handleHandShake(int clientId);
int sendData(int sockId, size_t size, void * msg);
void * receiveData(int sockId,size_t size);

