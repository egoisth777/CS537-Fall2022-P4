#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <math.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BLOCK_SIZE (4096)

/**
 * Check if the inum is valid and it is allocated
 * 
 * @param inum the inode number
 * @param numInode number of inodes
 * @param inode_bitmap the pointer to the start of the inode_bitmap
*/
int checkIfInumValid(int inum, int numInode, char *inode_bitmap)
{
    // Check if the bitmap's perspective agrees. If the allocated bit is 1.
    int bitmapOffsetNum = inum / 8;
    int bitmapOffset = inum % 8;
    if (inum < 0 || inum >= numInode || ((inode_bitmap[bitmapOffsetNum]) & (0x80 >> bitmapOffset) == 0))
        return -1;
    return 0;
}

void respondToServer(message reply, int replyNum, int sd, struct sockaddr_in *addr, int *rc)
{
    // sprintf(&(reply.msg), "%d", replyNum);
    reply.msg_code = replyNum;
    *rc = UDP_Write(sd, addr, (char *)&reply, sizeof(reply));
    printf("The Machine:: reply\n");
}


int 
findNoBlockAlloc(int offset, int nbytes) {
    return offset / BLOCK_SIZE == (offset + nbytes) / BLOCK_SIZE ? 1 : 2;
}

// Type: 0 databitmap 1 inodebitmap
int findEmptyDataBitmapSlot(char* bitmap, super_t superBlock, inode_t* metadata, int dataNumStartBlock) {
    int allocated = 0;
    // Find a place in data bitmap
        for (int j = 0; j < superBlock.data_region_len / 8; j++) //@TODO: not resolved, think of situation not divided by 8
        {
            if (allocated == 1)
            {
                break;
            }
            if (bitmap[j] & 0xFF != 0xFF)
            {
                int emptySlot = -1;
                int temp = bitmap[j];

                for (int x = 0; x < 8; x++)
                {
                    if ((temp >> x) - ((temp >> (x + 1)) << 1) == 0)
                    {
                        emptySlot = (8 - x) + j * 8;
                        bitmap[j] = bitmap[j] | (int)pwd(2, x);
                        metadata->direct[dataNumStartBlock] = emptySlot;
                        metadata->size += BLOCK_SIZE;
                        allocated = 1;
                        break;
                    }
                }
            }
        }
    return allocated;
}


// Type: 0 databitmap 1 inodebitmap
int findEmptyInodeBitmapSlot(char* bitmap, super_t superBlock, inode_t* inodes, int fileType, int pinum) {
    int allocated = 0;
    for (int j = 0; j < superBlock.inode_region_len / 8; j++) //@TODO: not resolved, think of situation not divided by 8
    {
        if (allocated == 1)
        {
            break;
        }
        if (bitmap[j] & 0xFF != 0xFF)
        {
            int emptySlot = -1;
            int temp = bitmap[j];

            for (int x = 0; x < 8; x++)
            {
                if ((temp >> x) - ((temp >> (x + 1)) << 1) == 0)
                {
                    emptySlot = (8 - x) + j * 8;
                    bitmap[j] = bitmap[j] | (int)pwd(2, x);
                    if (fileType == 0) // dir
                    {
                        inodes[emptySlot].type = 0;
                        inodes[emptySlot].size = BLOCK_SIZE;

                        // TODO : CREAT SB

                        /*
                        inodes[emptySlot].direct[0] = emptySlot;
                        inodes[emptySlot].direct[1] = pinum;
                        */
                        /*
                        for(int x = 0; x < 32; x ++)
                            inodes[emptySlot].direct[x] = -1;
                        */
                    }else {
                        inodes[emptySlot].type = 0;
                        inodes[emptySlot].size = 0;
                    }
                    allocated = 1;
                    break;
                }
            }
        }
    }
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

    // =============================================================================================
    // Read-in the super block
    int image_fd = open(fileImage, O_RDWR);
    struct stat sbuf;
    int rc = fstat(image_fd, &sbuf);
    assert(rc > -1);
    int image_size = (int) sbuf.st_size;
    void * image = mmap(NULL, image_size, PROT_READ | PROT_WRITE, MAP_SHARED, image_fd, 0);
    assert(image != MAP_FAILED);

    super_t *superBlock = (super_t *) image;

    // Sanity check
    printf("superBlock info\n inode_bitmap_addr: %d\n inode_bitmap_len: %d\n data_bitmap_addr: %d\n data_bitmap_len: %d\n inode_region_addr: %d\n inode_region_len: %d\n data_region_addr: %d\n data_region_len: %d\n",
           superBlock->inode_bitmap_addr, superBlock->inode_bitmap_len, superBlock->data_bitmap_addr,
            superBlock->data_bitmap_len, superBlock->inode_region_addr, superBlock->inode_region_len,
            superBlock->data_region_addr, superBlock->data_region_len);

    // Read-in the bitmaps
    char *inode_bitmap = image + superBlock->inode_bitmap_addr * BLOCK_SIZE;
    char *data_bitmap = image + superBlock->data_bitmap_len * BLOCK_SIZE;

    // Read-in the inode table
    inode_t *inode_table = image + superBlock->inode_region_addr * BLOCK_SIZE;

    // Read-in the data region
    char *data_region = image + superBlock->data_region_addr * BLOCK_SIZE;

    int numInode = superBlock->inode_region_len * BLOCK_SIZE / sizeof(inode_t);

    // Start the server
    while (1)
    {
        struct sockaddr_in addr;
        message received_msg;
        printf("The Machine:: waiting...\n");
        int rc = UDP_Read(sd, &addr, &received_msg, sizeof(message));
        printf("The Machine:: read message [size:%d contents:(%s)]\n", rc, received_msg.msg);
        if (rc > 0)
        {
            message reply_msg;
            char* msg = received_msg.msg;
            char buffer[BLOCK_SIZE] = received_msg.buf;
            int param1 = received_msg.param1;
            int param2 = received_msg.param2;
            int param3 = received_msg.param3;

            if (strcmp(msg, "MFS_Init") == 0) // Initialization
            {
                respondToServer(reply_msg, 0, sd, &addr, &rc);
            }
            else if (strcmp(msg, "MFS_Lookup") == 0)
            {
                int pinum = param1;
                char *name = received_msg.charParam;

                if (checkIfInumValid(pinum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                }
                else
                {
                    inode_t parent = inode_table[pinum];
                    int found = 0;
                    for (int i = 0; i < DIRECT_PTRS; i++)
                    {
                        unsigned int curr = parent.direct[i];
                        unsigned int comparison = -1;
                        if (curr == comparison)
                            continue;
                        char *namePosition = data_region + (BLOCK_SIZE * curr);
                        char currName[28];
                        for (int j = 0; j < BLOCK_SIZE / sizeof(dir_ent_t); j++)
                        {
                            memcpy(&currName, namePosition + (j * 32), 28);
                            int *inumPtr = (int *)(namePosition + (j * 32) + 28 * sizeof(char));
                            if (strcmp(currName, name) == 0)
                            {
                                found = 1;
                                respondToServer(reply_msg, &inumPtr, sd, &addr, &rc);
                                break;
                            }
                        }
                        if (found == 1)
                            break;
                    }
                    if (found == 0)
                        respondToServer(reply_msg, -1, sd, &addr, &rc);
                }
            }
            else if (strcmp(msg, "MFS_Stat") == 0)
            {
                int inum = param1;

                if (checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                }
                else
                {
                    inode_t information = inode_table[inum];
                    reply_msg.param1 = information.size; // size of the inode
                    reply_msg.param2 = information.type; // type of the inode
                    respondToServer(reply_msg, 0, sd, &addr, &rc);
                }
            }
            else if (strcmp(msg, "MFS_Write") == 0)
            {
                int inum = param1;
                int offset = param2;
                int nbytes = param3;

                if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset + nbytes >= BLOCK_SIZE * DIRECT_PTRS 
                    || checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                }
                else
                {
                    // Check if it is a regular file
                    inode_t metadata = inode_table[inum];
                    if (metadata.type == 0) // directory
                        respondToServer(reply_msg, -1, sd, &addr, &rc);
                    else
                    {
                        int numBlockToWrite = findNoBlockAlloc(offset, nbytes);    // Number of Data Block To Write: 1 or 2
                        unsigned int comparison = -1;

                        int numByteToWriteFirstBlock = numBlockToWrite== 2 ? BLOCK_SIZE - offset : nbytes;
                        int startAddrFirstBlockOffset = nbytes % BLOCK_SIZE;
                        int numByteToWriteSecondBlock = numBlockToWrite== 1 ? 0 : nbytes - numByteToWriteFirstBlock;

                        int locationFirstBlock = offset / BLOCK_SIZE;
                        int locationFirstBlockNum = metadata.direct[locationFirstBlock];
                        int locationSecondBlock = locationFirstBlock + 1; // prove this
                        int locationSecondBlockNum = metadata.direct[locationSecondBlock];

                        // Operation on First Block
                        int firstBlockAllocated = locationFirstBlockNum == comparison ? 0 : 1;
                        if (firstBlockAllocated == 0)
                        {
                            firstBlockAllocated = findEmptyDataBitmapSlot(data_bitmap, *superBlock, &metadata, locationFirstBlock);
                        }
                        locationFirstBlockNum = metadata.direct[locationFirstBlock];

                        if (firstBlockAllocated == -1)
                            respondToServer(reply_msg, -1, sd, &addr, &rc);
                        
                        char* startAddr = image + superBlock->data_region_addr + BLOCK_SIZE * locationFirstBlockNum + startAddrFirstBlockOffset;
                        // Write to persistency file
                        memcpy(startAddr, buffer, numByteToWriteFirstBlock);
                        msync(startAddr, numByteToWriteFirstBlock, MS_SYNC);

                        if (numBlockToWrite == 2) {
                            // Operation on Second Block
                            int secondBlockAllocated = locationSecondBlockNum == comparison ? 0 : 1;
                            if (secondBlockAllocated == 0)
                            {
                                secondBlockAllocated = findEmptyDataBitmapSlot(data_bitmap, *superBlock, &metadata, locationSecondBlock);
                            }
                            locationSecondBlockNum = metadata.direct[locationSecondBlock];

                            if (secondBlockAllocated == -1)
                                respondToServer(reply_msg, -1, sd, &addr, &rc);
                            
                            char* startAddr2 = image + superBlock->data_region_addr + BLOCK_SIZE * locationSecondBlockNum;
                            // Write to persistency file
                            memcpy(startAddr2, buffer, numByteToWriteSecondBlock);
                            msync(startAddr2, numByteToWriteSecondBlock, MS_SYNC);
                        }
                        respondToServer(reply_msg, 0, sd, &addr, &rc);
                    }
                }
            }
            else if (strcmp(msg, "MFS_Read") == 0)
            {
                int inum = param1;
                int offset = param2;
                int nbytes = param3;

                if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset > BLOCK_SIZE * DIRECT_PTRS || checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                {
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                    continue;
                }
                inode_t targetInode = inode_table[inum];
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
                fsync(image_fd);
                sprintf(reply, "stopping!");
                rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
                printf("The Machine:: reply\n");
                exit(0);
            }
        }
    }

    return 0;
}
