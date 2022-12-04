#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"

#define BUFFER_SIZE (1000)

int initialized = 0;
char* host;
int portNum;
struct sockaddr_in addrSnd, addrRcv;
int sd = 0;

int MFS_Init(char *hostname, int port)
{
    sd = UDP_Open(20000);
    int rc = UDP_FillSockAddr(&addrSnd, hostname, port);

    char message[BUFFER_SIZE];
    sprintf(message, "TOMACHINE,initialization");

    printf("Client Initalizing :: [%s]\n", message);
    rc = UDP_Write(sd, &addrSnd, message, BUFFER_SIZE);
    while (rc < 0) {
        printf("Client :: failed to send, retrying\n");
        exit(1);
    }

    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, message, BUFFER_SIZE);
    printf("client:: got reply [size:%d contents:(%s)\n", rc, message);
    initialized = 1;
    host = hostname;
    portNum = port;
    return 0;
}
int MFS_Lookup(int pinum, char *name)
{
    return 0;
}
int MFS_Stat(int inum, MFS_Stat_t *m)
{
    return 0;
}
int MFS_Write(int inum, char *buffer, int offset, int nbytes)
{
    return 0;
}
int MFS_Read(int inum, char *buffer, int offset, int nbytes)
{
    return 0;
}
int MFS_Creat(int pinum, int type, char *name)
{
    return 0;
}
int MFS_Unlink(int pinum, char *name)
{
    return 0;
}
int MFS_Shutdown()
{
    return 0;
}

int main(int argc, char const *argv[])
{
    printf("Fuck you World!\n");
    return 0;
}
