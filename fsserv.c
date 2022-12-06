#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"

#define BUFFER_SIZE 5000
#define BLOCK_SIZE (4096)

#define SPLITTER "~"

int checkIfInumValid(int inum, int numInode, char *inode_bitmap)
{
    // Check if the bitmap's perspective agrees. If the allocated bit is 1.
    int bitmapOffsetNum = inum / 8;
    int bitmapOffset = inum % 8;
    if (inum < 0 || inum >= numInode || ((inode_bitmap[bitmapOffsetNum]) & (0x80 >> bitmapOffset) == 0))
        return -1;
    return 0;
}

void respondToServer(char **reply, int replyNum, int sd, struct sockaddr_in *addr, int *rc)
{
    sprintf(*reply, "%d", replyNum);
    *rc = UDP_Write(sd, addr, *reply, BUFFER_SIZE);
    printf("The Machine:: reply\n");
}

// Important variables:
// int fileImageFileDescriptor = open(fileImage, O_RDWR);
// super_t superBlock
// char* inode_bitmap
// char* data_bitmap
// int* inode_table
// inode_t* inode_table_struct
// char* data_region
int main(int argc, char const *argv[])
{
    // Get length of the argument
    int length = 0;
    char const *curr = argv[0];
    while (curr != NULL)
    {
        length++;
        curr = argv[length];
    }

    // server [portnum] [image], argument number has to be 3.
    assert(length == 3);

    // Convert arguments to variables of port number and file image filename.
    int portnum = atoi(argv[1]);
    char const *fileImage = argv[2];

    // Sanity check
    printf("portnum: %d\nfileImage: %s \n", portnum, fileImage);

    // If the file Sys doesn't exist, do this.
    if (access(fileImage, F_OK) != 0)
    {
        printf("image does not exist\n");
        exit(1);
    }

    // Establish listening on portnum
    int sd = UDP_Open(portnum);
    assert(sd > -1);

    // Read-in the super block
    int fileImageFileDescriptor = open(fileImage, O_RDWR);
    int *buf = malloc(sizeof(int) * 1024); // assume int is 4-byte size
    lseek(fileImageFileDescriptor, 0, SEEK_SET);
    ssize_t numRead = read(fileImageFileDescriptor, buf, BLOCK_SIZE); // read first 4096 bytes (super block)
    if (numRead == -1)
    { // read failed
        exit(1);
    }
    super_t superBlock;

    superBlock.inode_bitmap_addr = buf[0];
    superBlock.inode_bitmap_len = buf[1];
    superBlock.data_bitmap_addr = buf[2];
    superBlock.data_bitmap_len = buf[3];
    superBlock.inode_region_addr = buf[4];
    superBlock.inode_region_len = buf[5];
    superBlock.data_region_addr = buf[6];
    superBlock.data_region_len = buf[7];

    free(buf);

    // Sanity check
    printf("superBlock info\n inode_bitmap_addr: %d\n inode_bitmap_len: %d\n data_bitmap_addr: %d\n data_bitmap_len: %d\n inode_region_addr: %d\n inode_region_len: %d\n data_region_addr: %d\n data_region_len: %d\n",
           superBlock.inode_bitmap_addr, superBlock.inode_bitmap_len, superBlock.data_bitmap_addr, superBlock.data_bitmap_len, superBlock.inode_region_addr, superBlock.inode_region_len, superBlock.data_region_addr, superBlock.data_region_len);

    // Read-in the bitmaps
    char *inode_bitmap = malloc(sizeof(char) * BLOCK_SIZE * superBlock.inode_bitmap_len);
    char *data_bitmap = malloc(sizeof(char) * BLOCK_SIZE * superBlock.data_bitmap_len);

    lseek(fileImageFileDescriptor, BLOCK_SIZE * superBlock.inode_bitmap_addr, SEEK_SET);
    ssize_t numReadInodeBitmap = read(fileImageFileDescriptor, inode_bitmap, BLOCK_SIZE * superBlock.inode_bitmap_len);
    lseek(fileImageFileDescriptor, BLOCK_SIZE * superBlock.data_bitmap_addr, SEEK_SET);
    ssize_t numReadDataBitmap = read(fileImageFileDescriptor, data_bitmap, BLOCK_SIZE * superBlock.data_bitmap_len);

    // Read-in the inode table
    int *inode_table = malloc(sizeof(int) * (BLOCK_SIZE / sizeof(int)) * superBlock.inode_region_len);
    int numInode = BLOCK_SIZE * superBlock.inode_region_len / sizeof(inode_t);
    inode_t *inode_table_struct = malloc(sizeof(inode_t) * numInode);
    for (int i = 0; i < numInode; i++)
    {
        inode_t curr;
        curr.type = inode_table[sizeof(inode_t) * i];
        curr.size = inode_table[sizeof(inode_t) * i + 1];
        for (int j = 0; j < DIRECT_PTRS; j++)
            curr.direct[j] = inode_table[sizeof(inode_t) * i + 2 + j];
    }

    lseek(fileImageFileDescriptor, BLOCK_SIZE * superBlock.inode_region_addr, SEEK_SET);
    ssize_t numReadInodeTable = read(fileImageFileDescriptor, inode_table, BLOCK_SIZE * superBlock.inode_region_len);

    // Read-in the data region
    char *data_region = malloc(sizeof(char) * BLOCK_SIZE * superBlock.data_region_len);

    lseek(fileImageFileDescriptor, BLOCK_SIZE * superBlock.data_region_addr, SEEK_SET);
    ssize_t numReadDataRegion = read(fileImageFileDescriptor, data_region, BLOCK_SIZE * superBlock.data_region_len);

    // Start the server

    while (1)
    {
        struct sockaddr_in addr;
        char message[BUFFER_SIZE];
        printf("The Machine:: waiting...\n");
        int rc = UDP_Read(sd, &addr, message, BUFFER_SIZE);
        printf("The Machine:: read message [size:%d contents:(%s)]\n", rc, message);
        if (rc > 0)
        {
            char *keys[BUFFER_SIZE];
            int splitIndex = 0;
            char *split = strtok(message, SPLITTER);

            for (; split != NULL; splitIndex++)
            {
                keys[splitIndex] = split;
                split = strtok(NULL, SPLITTER);
            }

            assert(strcmp(keys[0], "TOMACHINE") == 0); // PROTOCOL REQUIREMENT
            char reply[BUFFER_SIZE];
            if (strcmp(keys[1], "MFS_Init") == 0) // Initialization
            {
                sprintf(reply, "init success!");
                rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
                printf("The Machine:: reply\n");
            }
            else if (strcmp(keys[1], "MFS_Lookup") == 0)
            {
                int pinum = atoi(keys[2]);
                char *name = keys[3];

                if (checkIfInumValid(pinum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(&reply, -1, sd, &addr, &rc);
                }
                else
                {
                    inode_t parent = inode_table_struct[pinum];
                    int found = 0;
                    for (int i = 0; i < DIRECT_PTRS; i++)
                    {
                        unsigned int curr = parent.direct[i];
                        unsigned int comparison = -1;
                        if (curr == comparison)
                            continue;
                        char *namePosition = data_region + (BLOCK_SIZE * curr);
                        char currName[28];
                        for (int j = 0; j < BLOCK_SIZE / 32; j++)
                        {
                            memcpy(&currName, namePosition + (j * 32), 28);
                            int *inumPtr = (int *)(namePosition + (j * 32) + 28 * sizeof(char));
                            if (strcmp(currName, name) == 0)
                            {
                                found = 1;
                                respondToServer(&reply, &inumPtr, sd, &addr, &rc);
                                break;
                            }
                        }
                        if (found == 1)
                            break;
                    }
                    if (found == 0)
                        respondToServer(&reply, -1, sd, &addr, &rc);
                }
            }
            else if (strcmp(keys[1], "MFS_Stat") == 0)
            {
                int inum = atoi(keys[2]);
                MFS_Stat_t *m = (MFS_Stat_t *)atoi(keys[3]);

                if (checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(&reply, -1, sd, &addr, &rc);
                }
                else
                {
                    inode_t information = inode_table_struct[inum];
                    m->size = information.size;
                    m->type = information.type;
                    respondToServer(&reply, 0, sd, &addr, &rc);
                }
            }
            else if (strcmp(keys[1], "MFS_Write") == 0)
            {
                int inum = atoi(keys[2]);
                char *buffer = keys[3];
                int offset = atoi(keys[4]);
                int nbytes = atoi(keys[5]);

                if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset > BLOCK_SIZE * DIRECT_PTRS || checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(&reply, -1, sd, &addr, &rc);
                }
                else
                {
                    // Check if it is a regular file
                    inode_t information = inode_table_struct[inum];
                    if (information.type == 0) // directory
                        respondToServer(&reply, -1, sd, &addr, &rc);
                    else
                    {
                        int numStartBlock = offset / BLOCK_SIZE;                          // determine which block to be written
                        unsigned int BlockNumToWrite = information.direct[numStartBlock]; // Number of Data Block To Write
                        unsigned int comparison = -1;
                        int allocated = 0 ? BlockNumToWrite == comparison : 1;
                        if (BlockNumToWrite == comparison)
                        {
                            // Find a place in data bitmap
                            for (int j = 0; j < superBlock.data_region_len / 8; j++) //@TODO: not resolved, think of situation not divided by 8
                            {
                                if (allocated == 1)
                                {
                                    break;
                                }
                                if (data_bitmap[j] & 0xFF != 0xFF)
                                {
                                    int emptySlot = -1;
                                    int temp = data_bitmap[j];

                                    for (int x = 0; x < 8; x++)
                                    {
                                        if ((temp >> x) - ((temp >> (x + 1)) << 1) == 0)
                                        {
                                            emptySlot = (8 - x) + j * 8;
                                            data_bitmap[j] = data_bitmap[j] | (int)pwd(2, x);
                                            lseek(fileImageFileDescriptor, superBlock.data_bitmap_addr * BLOCK_SIZE, SEEK_SET);
                                            write(fileImageFileDescriptor, data_bitmap, superBlock.data_bitmap_len * BLOCK_SIZE);
                                            allocated = 1;
                                            information.direct[numStartBlock] = emptySlot;
                                            information.size = information.size + BLOCK_SIZE;
                                            lseek(fileImageFileDescriptor, superBlock.inode_region_addr * BLOCK_SIZE, SEEK_SET);
                                            write(fileImageFileDescriptor, inode_table_struct, superBlock.inode_region_len * BLOCK_SIZE);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if (allocated == -1)
                            respondToServer(&reply, -1, sd, &addr, &rc);
                        int startAddr = superBlock.data_region_addr + BLOCK_SIZE * BlockNumToWrite;
                        // Write to persistency file
                        lseek(fileImageFileDescriptor, startAddr, SEEK_SET);
                        write(fileImageFileDescriptor, buffer, nbytes);
                        fsync(fileImageFileDescriptor);
                        // Write to memory
                        char *startAddrMem = BLOCK_SIZE * BlockNumToWrite;
                        memcpy(startAddrMem, buffer, nbytes);
                        respondToServer(&reply, 0, sd, &addr, &rc);
                    }
                }
            }
            else if (strcmp(keys[1], "MFS_Read") == 0)
            {
                int inum = atoi(keys[2]);
                // char *buffer = keys[3];
                int offset = atoi(keys[4]);
                int nbytes = atoi(keys[5]);

                if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset > BLOCK_SIZE * DIRECT_PTRS || checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(&reply, -1, sd, &addr, &rc);
                    continue;
                }
                inode_t targetInode = inode_table_struct[inum];
                if (targetInode.type == 0)
                {   // directory
                    int dir_size = targetInode.size / 32;
                    
                }
                int numStartBlock = offset / BLOCK_SIZE; // determine which block to be written
                int numEndBlock = (offset + nbytes) /BLOCK_SIZE; // end no. of the block
                if(numEndBlock == numStartBlock){ //case where you only have to read one block
                    unsigned int BlockNumToRead = targetInode.direct[numStartBlock];
                    // unsigned int comparison = -1;
                    if ((unsigned int) (-1) == BlockNumToRead)
                    {
                        respondToServer(&reply, -1, sd, &addr, &rc);
                        continue;
                    }
                    char* tmp = data_region + BlockNumToRead * BLOCK_SIZE;
                    char msg2[1 + nbytes] ;
                    msg2[0] = '1';
                    strncpy(msg2+1, tmp, nbytes);
                    rc = UDP_Write(sd, &addr, *msg2, BUFFER_SIZE);
                    
                }else{ // case where you have to read two blocks

                }
                                          
                unsigned int BlockNumToRead = targetInode.direct[numStartBlock]; // Number of Data Block To read

                
                
            }
            else if (strcmp(keys[1], "MFS_Creat") == 0)
            {
            }
            else if (strcmp(keys[1], "MFS_Unlink") == 0)
            {
            }
            else if (strcmp(keys[1], "MFS_Shutdown") == 0)
            {
                fsync(fileImageFileDescriptor);
                sprintf(reply, "stopping!");
                rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
                printf("The Machine:: reply\n");
                exit(0);
            }
        }
    }

    return 0;
}
