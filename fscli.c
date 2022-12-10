#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/select.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BUFFER_SIZE (1000)

int initialized = 0;
char* host;
int portNum;
struct sockaddr_in addrSnd, addrRcv;
int s_descriptor = -1;
struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

int sendToServer(int sd, struct timeval tv, message forward_msg, message *received_msg, struct sockaddr_in addrSnd, struct sockaddr_in addrRcv)
{
    if (initialized == 0)
    {
        printf("Not Initalized. Initialize and Try Again\n");
        return -1;
    }
    int res = 0;
    int rc = 0;
    int msg_code = -1;
    while (res <= 0 || rc < 0)
    {
        // retry
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sd, &rd);
        tv.tv_sec = 5;

        printf("client:: send message [%s]\n", forward_msg.msg);
        rc = UDP_Write(sd, &addrSnd, (char*)&forward_msg, BUFFER_SIZE);
        if (rc < 0) {
            printf("client:: failed to send\n");
            continue;
        }
        printf("client:: wait for reply...\n");
        printf("sd = %d\n", sd);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        if (res <= 0) {
            printf("err / timeout\n");
            continue;
        }
        rc = UDP_Read(sd, &addrRcv, (char*)received_msg, BUFFER_SIZE);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        if (rc < 0) {
            printf("client:: failed to operate\n");
            continue;
        }
        msg_code = received_msg->msg_code;
    }
    printf("client:: got reply [size:%d code:(%s)\n", rc, msg_code);
    return msg_code;
}

int MFS_Init(char *hostname, int port)
{
    int sd = UDP_Open(20000);
    
    message forward_msg = {.msg = "MFS_Init"};
    message receive_msg;

    int res = 0;
    int rc = 0;
    int msg_code = -1;
    while (res <= 0 || rc < 0)
    {
        // retry
        rc = UDP_FillSockAddr(&addrSnd, hostname, port);
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sd, &rd);
        tv.tv_sec = 5;

        printf("client:: send message [%s]\n", msg.msg);
        rc = UDP_Write(sd, &addrSnd, (char*)&msg, BUFFER_SIZE);
        if (rc < 0) {
            printf("client:: failed to send\n");
            continue;
        }
        printf("client:: wait for reply...\n");
        printf("sd = %d\n", sd);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        if (res <= 0) {
            printf("err / timeout\n");
            continue;
        }
        rc = UDP_Read(sd, &addrRcv, (char*)&receive_msg, BUFFER_SIZE);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        printf("res: %d\n, rc = %d \n", res, rc);
        if (rc < 0) {
            printf("client:: failed to operate\n");
            continue;
        }
        msg_code = receive_msg.msg_code;
    }

    if (msg_code == 0) {
        // successful
        portNum = port;
        host = hostname;
        initialized = 1;
        s_descriptor = sd;
    }

    printf("client:: got reply [size:%d code:(%s)\n", rc, msg_code);
    return 0;
    
}
int MFS_Lookup(int pinum, char *name)
{
    message forward_msg = {.msg = "MFS_Lookup", .param1 = pinum, .charParam = name};
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}
int MFS_Stat(int inum, MFS_Stat_t *m)
{
    message forward_msg = {.msg = "MFS_Stat", .param1 = inum};
    message received_msg;
    int msg_code = sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
    if (msg_code == -1)
        return msg_code;
    m->size = received_msg.param1;
    m->type = received_msg.param2;
    return msg_code;
}
int MFS_Write(int inum, char *buffer, int offset, int nbytes)
{
    message forward_msg = {.msg = "MFS_Write", .param1 = inum, .buf = buffer, .param2 = offset, .param3 = nbytes};
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}
int MFS_Read(int inum, char *buffer, int offset, int nbytes)
{
    message forward_msg = {.msg = "MFS_Read", .param1 = inum, .param2 = offset, .param3 = nbytes};
    message received_msg;
    int msg_code = sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
    if (msg_code == -1)
        return msg_code;
    // buffer = malloc(sizeof(char) * nbytes);
    memcpy(buffer, received_msg.buf, nbytes);
    return msg_code;
}
int MFS_Creat(int pinum, int type, char *name)
{
    message forward_msg = {.msg = "MFS_Creat", .param1 = pinum, .param2 = type, .charParam = name};
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}
int MFS_Unlink(int pinum, char *name)
{
    message forward_msg = {.msg = "MFS_Unlink", .param1 = pinum, .charParam = name};
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}
int MFS_Shutdown()
{
    message forward_msg = {.msg = "MFS_Shutdown"};
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}

int main(int argc, char const *argv[])
{
    printf("Fuck you World!\n");
    MFS_Init("localhost", 3000);
    sleep(5);
    MFS_Shutdown();
    return 0;
}
