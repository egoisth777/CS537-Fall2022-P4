#include <stdio.h>
#include "udp.h"
#include "message.h"

#define BUFFER_SIZE (1000)
struct timeval *restrict tv;
// client code
int main(int argc, char *argv[]) {
    
    struct sockaddr_in addrSnd, addrRcv;

    int sd = UDP_Open(20000);
    int rc = UDP_FillSockAddr(&addrSnd, "localhost", 10000);
    tv->tv_sec = 2;
    message forward_msg_struct;
    sprintf(forward_msg_struct.buf, "hello world");

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(sd, &rd);

    printf("client:: send message [%s]\n", forward_msg_struct.msg);
    rc = UDP_Write(sd, &addrSnd, &forward_msg_struct, BUFFER_SIZE);
    if (rc < 0) {
        printf("client:: failed to send\n");
        select(sd + 1, &rd, NULL, NULL, tv);
    }

    printf("client:: wait for reply...\n");
    rc = UDP_Read(sd, &addrRcv, &forward_msg_struct, BUFFER_SIZE);
    if (rc <= 0) {
        printf("timeout\n");
        select(sd + 1, &rd, NULL, NULL, tv);
    }
    printf("client:: got reply [size:%d contents:(%s)\n", rc, forward_msg_struct.msg);
    return 0;
}
