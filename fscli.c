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

#define BUFFER_SIZE (4192)

int initialized = 0;
char* host;
int portNum;
struct sockaddr_in addrSnd, addrRcv;
int s_descriptor = -1;
struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

// create a socket and bind it to a port on the current machine
// used to listen for incoming packets
int UDP_Open(int port) {
    int fd;           
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	perror("socket");
	return 0;
    }

    // set up the bind
    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));

    my_addr.sin_family      = AF_INET;
    my_addr.sin_port        = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *) &my_addr, sizeof(my_addr)) == -1) {
	perror("bind");
	close(fd);
	return -1;
    }

    return fd;
}

// fill sockaddr_in struct with proper goodies
int UDP_FillSockAddr(struct sockaddr_in *addr, char *hostname, int port) {
    bzero(addr, sizeof(struct sockaddr_in));
    if (hostname == NULL) {
	return 0; // it's OK just to clear the address
    }
    
    addr->sin_family = AF_INET;          // host byte order
    addr->sin_port   = htons(port);      // short, network byte order

    struct in_addr *in_addr;
    struct hostent *host_entry;
    if ((host_entry = gethostbyname(hostname)) == NULL) {
	perror("gethostbyname");
	return -1;
    }
    in_addr = (struct in_addr *) host_entry->h_addr;
    addr->sin_addr = *in_addr;

    return 0;
}

int UDP_Write(int fd, struct sockaddr_in *addr, char *buffer, int n) {
    int addr_len = sizeof(struct sockaddr_in);
    int rc = sendto(fd, buffer, n, 0, (struct sockaddr *) addr, addr_len);
    return rc;
}

int UDP_Read(int fd, struct sockaddr_in *addr, char *buffer, int n) {
    int len = sizeof(struct sockaddr_in); 
    int rc = recvfrom(fd, buffer, n, 0, (struct sockaddr *) addr, (socklen_t *) &len);
    // assert(len == sizeof(struct sockaddr_in)); 
    return rc;
}

int UDP_Close(int fd) {
    return close(fd);
}

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

        printf("client:: send message [%s], rc: %d\n", forward_msg.msg, rc);
        rc = UDP_Write(sd, &addrSnd, (char*)&forward_msg, BUFFER_SIZE);
        if (rc < 0) {
            printf("client:: failed to send\n");
            sleep(5);
            continue;
        }
        printf("client:: wait for reply...\n");
        printf("sd = %d\n", sd);
        res = select(sd + 1, &rd, NULL, NULL, &tv);
        rc = UDP_Read(sd, &addrRcv, (char*)received_msg, BUFFER_SIZE);
        
        printf("res: %d\n, rc = %d \n", res, rc);
        if (rc < 0) {
            printf("client:: failed to operate\n");
            continue;
        }
        if (res <= 0) {
            printf("err / timeout\n");
            continue;
        }
        msg_code = received_msg->msg_code;
    }
    printf("client:: got reply [size:%d code:(%d)\n", rc, msg_code);
    return msg_code;
}

int MFS_Init(char *hostname, int port)
{
    int sd = -1;
    while (sd <= -1) {
        int porta = rand() % 20001;
        sd = UDP_Open(porta);
    }
    
    message forward_msg = {.msg = "MFS_Init"};
    message receive_msg;

    int res = 0;
    int rc = 0;
    int msg_code = -1;
    while (res <= 0 || rc < 0)
    {
        printf("res: %d, rc = %d \n", res, rc);
        // retry
        rc = UDP_FillSockAddr(&addrSnd, hostname, port);
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(sd, &rd);
        tv.tv_sec = 5;
        printf("client:: send message [%s], rc: %d\n", forward_msg.msg, rc);

        rc = UDP_Write(sd, &addrSnd, (char*)&forward_msg, BUFFER_SIZE);
        if (rc < 0) {
            printf("client:: failed to send\n");
            sleep(5);
            continue;
        }
        printf("client:: wait for reply...\n");
        printf("sd = %d\n", sd);

        res = select(sd + 1, &rd, NULL, NULL, &tv);
        rc = UDP_Read(sd, &addrRcv, (char*)&receive_msg, BUFFER_SIZE);
        
        printf("res: %d, rc = %d \n", res, rc);
        if (rc < 0) {
            printf("client:: failed to operate\n");
            continue;
        }
        if (res <= 0) {
            printf("err / timeout\n");
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

    printf("client:: got reply [size:%d code:(%d)\n", rc, msg_code);
    return 0;
    
}
int MFS_Lookup(int pinum, char *name)
{
    message forward_msg = {.msg = "MFS_Lookup", .param1 = pinum};
    memcpy(&forward_msg.charParam, name, 48);
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
    printf("inum:%d, msize: %d, mtype: %d\n", inum, m->size, m->type);
    return msg_code;
}
int MFS_Write(int inum, char *buffer, int offset, int nbytes)
{
    message forward_msg = {.msg = "MFS_Write", .param1 = inum, .param2 = offset, .param3 = nbytes};
    memcpy(&forward_msg.buf, buffer, 4096);
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
    message forward_msg = {.msg = "MFS_Creat", .param1 = pinum, .param2 = type};
    memcpy(&forward_msg.charParam, name, 48);
    message received_msg;
    return sendToServer(s_descriptor, tv, forward_msg, &received_msg, addrSnd, addrRcv);
}
int MFS_Unlink(int pinum, char *name)
{
    message forward_msg = {.msg = "MFS_Unlink", .param1 = pinum};
    memcpy(&forward_msg.charParam, name, 48);
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
    MFS_Init("localhost", 3000);
    // char* name = "testa";
    // MFS_Creat(0, 1, name);
    sleep(5);
    MFS_Shutdown();
    return 0;
}
