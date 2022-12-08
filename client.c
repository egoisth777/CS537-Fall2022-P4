#include <stdio.h>
#include "udp.h"
#include "message.h"

#define BUFFER_SIZE (1000)
struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
// client code
int main(int argc, char *argv[]) {
    
    struct sockaddr_in addrSnd, addrRcv;
    
    int sd = UDP_Open(20000);
    int rc = UDP_FillSockAddr(&addrSnd, "localhost", 10000);
    message forward_msg_struct;
    sprintf(forward_msg_struct.buf, "hello world");

    
    int res = 0;
    while (res <= 0 || rc < 0)
    {
        // retry
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sd, &rd);

        printf("client:: send message [%s]\n", forward_msg_struct.msg);
        rc = UDP_Write(sd, &addrSnd, (char*)&forward_msg_struct, BUFFER_SIZE);
        if (rc < 0) {
            printf("client:: failed to send\n");
            continue;
        }
        printf("client:: wait for reply...\n");
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        rc = UDP_Read(sd, &addrRcv, (char*)&forward_msg_struct, BUFFER_SIZE);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        if (rc < 0) {
            printf("client:: failed to operate\n");
            continue;
        }
    }

    printf("client:: got reply [size:%d contents:(%s)\n", rc, forward_msg_struct.msg);
    return 0;
}
