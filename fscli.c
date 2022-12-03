#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"

int MFS_Init(char *hostname, int port)
{
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
