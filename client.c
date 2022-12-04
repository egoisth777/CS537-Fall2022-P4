#include <stdio.h>
#include "udp.h"

#define BUFFER_SIZE (1000)
struct timeval *restrict tv;
// client code
int main(int argc, char *argv[]) {
    
    struct sockaddr_in addrSnd, addrRcv;

    int sd = UDP_Open(20000);
    int rc = UDP_FillSockAddr(&addrSnd, "localhost", 10000);
    tv->tv_sec = 2;
    char message[BUFFER_SIZE];
    sprintf(message, "hello world");

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(sd, &rd);

    printf("client:: send message [%s]\n", message);
    rc = UDP_Write(sd, &addrSnd, message, BUFFER_SIZE);
    if (rc < 0) {
        printf("client:: failed to send\n");
        select(sd + 1, &rd, NULL, NULL, tv);
    }

    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, message, BUFFER_SIZE);
    if (rc <= 0) {
        printf("timeout\n");
        select(sd + 1, &rd, NULL, NULL, tv);
    }
    printf("client:: got reply [size:%d contents:(%s)\n", rc, message);
    return 0;
}
