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



/**
 * @brief find an empty spot in the Inode Bitmap, allocate it. Success will return 1, failure will return 0
 * 
 * @param bitmap the starting address of the Inode bitmap
 * @param superBlock pointer to the super block
 * @param emptySlot index of the emptySlot number
 * @return int success : 1, failure 0
 */
int 
findEmptyInodeBitmapSlot(char *bitmap, super_t superBlock, int * emptySlot)
{
    int allocated = 0;
    for (int j = 0; j < superBlock.inode_region_len / 8; j++)
    {
        if (allocated == 1)
        {
            break;
        }
        if (bitmap[j] & 0xFF != 0xFF)
        {
            *emptySlot = -1;
            int temp = bitmap[j];

            for (unsigned int x = 0; x < 8; x++)
            {
                if ((temp >> x) - ((temp >> (x + 1)) << 1) == 0)
                {
                    *emptySlot = (8 - x) + j * 8;
                    bitmap[j] = bitmap[j] | (int)pwd(2, x); // set the bitmap
                    allocated = 1;
                    break;
                }
            }
        }
    }
    return allocated;
}

int lookup(int pinum, char * name, inode_t * inode_table, char* data_region, int * inumPtr)
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
                *inumPtr = *(int *)(namePosition + (j * 32) + 28 * sizeof(char));
                if (strcmp(currName, name) == 0)
                {
                    found = 1;
                    break;
                }
            }
            if (found == 1)
                break;
        }
        return found;
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
                    respondToServer(reply_msg, -1, sd, &addr, rc);
                }
                else{
                    int * inumPtr;
                    int found = lookup(pinum, name, inode_table, data_region, inumPtr);
                    found = -1 ? found == 0 : *inumPtr;
                    respondToServer(reply_msg, found, sd, &addr, rc);
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
                    if (metadata.type == 0) // directory {}
                        respondToServer(reply_msg, -1, sd, &addr, &rc);
                    else
                    {
                        int numBlockToWrite = findNoBlockAlloc(offset, nbytes);    // Number of Data Block To Write: 1 or 2
                        unsigned int comparison = -1;

                        int numByteToWriteFirstBlock = numBlockToWrite== 2 ? BLOCK_SIZE - offset : nbytes;
                        int startAddrFirstBlockOffset = nbytes % BLOCK_SIZE;
                        int numByteToWriteSecondBlock = numBlockToWrite== 1 ? 0 : nbytes - numByteToWriteFirstBlock;

                        int locationFirstBlock = offset / BLOCK_SIZE;
                        unsigned locationFirstBlockNum = metadata.direct[locationFirstBlock];
                        int locationSecondBlock = locationFirstBlock + 1; // prove this
                        unsigned locationSecondBlockNum = metadata.direct[locationSecondBlock];

                        // Operation on First Block
                        int firstBlockAllocated = locationFirstBlockNum == comparison ? 0 : 1;
                        if (firstBlockAllocated == 0)
                        {
                            firstBlockAllocated = findEmptyDataBitmapSlot(data_bitmap, *superBlock, &metadata, locationFirstBlock);
                        }
                        locationFirstBlockNum = metadata.direct[locationFirstBlock];

                        if (firstBlockAllocated == 0) {
                            respondToServer(reply_msg, -1, sd, &addr, &rc);
                            continue;
                        }
                        
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

                            if (secondBlockAllocated == 0)
                                respondToServer(reply_msg, -1, sd, &addr, &rc);
                            
                            char* startAddr2 = image + superBlock->data_region_addr + BLOCK_SIZE * locationSecondBlockNum;
                            // Write to persistency file
                            memcpy(startAddr2, buffer, numByteToWriteSecondBlock);
                            msync(startAddr2, numByteToWriteSecondBlock, MS_SYNC);
                        }
                        msync(inode_bitmap, superBlock->inode_bitmap_len * BLOCK_SIZE, MS_SYNC);
                        msync(data_bitmap, superBlock->data_bitmap_len * BLOCK_SIZE, MS_SYNC);
                        respondToServer(reply_msg, 0, sd, &addr, &rc);
                    }
                }
            }
            else if (strcmp(msg, "MFS_Read") == 0)
            {
                int inum = param1;
                int offset = param2;
                int nbytes = param3;

                if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 
                || offset > BLOCK_SIZE * DIRECT_PTRS || checkIfInumValid(inum, numInode, inode_bitmap) == -1)
                { 
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                    continue;
                }
                inode_t metadata = inode_table[inum];
                if (metadata.type == 0) { // directory
                    int dir_size = sizeof(dir_ent_t);
                    if (offset % dir_size != 0 || (offset + nbytes) % dir_size != 0){
                        respondToServer(reply_msg, -1, sd, &addr, &rc);
                        continue;
                    }
                }
                int no_block_to_read = findNoBlockAlloc(offset, nbytes);    // how many blocks will be read
                unsigned int comparison = -1;
                int no_bytes_to_read_1 = no_block_to_read == 2 ? BLOCK_SIZE - offset : nbytes; // number of bytes to be read from first block
                
                int startAddrFirstBlockOffset = nbytes % BLOCK_SIZE; // startin addr of read in first block
                int no_bytes_to_read_2 = no_block_to_read == 1 ? 0 : nbytes - no_bytes_to_read_1; // number of bytes to be read from second block

                int locationFirstBlock = offset / BLOCK_SIZE;
                unsigned locationFirstBlockNum = metadata.direct[locationFirstBlock];
                int locationSecondBlock = locationFirstBlock + 1; // prove this
                unsigned locationSecondBlockNum = metadata.direct[locationSecondBlock];

                // Operation on First Block
                int firstBlockAllocated = locationFirstBlockNum == comparison ? 0 : 1;
                if (firstBlockAllocated == 0)
                {
                    respondToServer(reply_msg, -1, sd, &addr, &rc);
                    continue;
                }

                char* startAddr = image + superBlock->data_region_addr + BLOCK_SIZE * locationFirstBlockNum + startAddrFirstBlockOffset;
                // Read
                memcpy(reply_msg.buf, startAddr, no_bytes_to_read_1);

                if (no_block_to_read == 2) {
                    // Operation on Second Block
                    int secondBlockAllocated = locationSecondBlockNum == comparison ? 0 : 1;
                    if (secondBlockAllocated == 0)
                    {
                        respondToServer(reply_msg, -1, sd, &addr, &rc);
                        continue;
                    }
                    
                    char* startAddr2 = image + superBlock->data_region_addr + BLOCK_SIZE * locationSecondBlockNum;
                    // Read
                    memcpy(reply_msg.buf + no_bytes_to_read_1, startAddr2, no_bytes_to_read_2);
                }
                respondToServer(reply_msg, 0, sd, &addr, &rc);   
            }
            else if (strcmp(msg, "MFS_Creat") == 0)
            {
                int pinum  = param1;
                int type   = param2;
                char * name = received_msg.charParam;

                if (checkIfInumValid(pinum, numInode, inode_bitmap) == -1 || strlen(name) > 28) 
                { //check if the pinum is valid
                    respondToServer(reply_msg, -1, sd, &addr, rc);
                    continue;
                }
                int *inumPtr;
                if (lookup(pinum, name, inode_table, data_region, inumPtr) == 0) {
                    respondToServer(reply_msg, 0, sd, &addr, rc);
                    continue;
                }
                inode_t metadata = inode_table[pinum]; // meta data of the file/dir to create
                if(metadata.type == 1){ // cannot create a file inside a file
                    respondToServer(reply_msg, -1, sd, &addr, rc);
                    continue;
                }

                int * emptySlot;
                if (findEmptyInodeBitmapSlot(inode_bitmap, *superBlock, emptySlot) == 0)
                { // allocation failure, not enough spot
                    respondToServer(reply_msg, -1, sd, &addr, rc);
                    continue;
                }
                if( type == 0 )
                { // create a directory
                    inode_table[*emptySlot].size = BLOCK_SIZE;
                    inode_table[*emptySlot].type = 0;
                    if(!findEmptyDataBitmapSlot(data_bitmap, *superBlock, inode_table + *emptySlot, 0)){
                        respondToServer(reply_msg, -1, sd, &addr, rc);
                        continue;
                    }
                    int datablock_no = inode_table[*emptySlot].direct[0];
                    MFS_DirEnt_t self = {
                        ".", *emptySlot
                    };
                    MFS_DirEnt_t parent = {
                        "..", pinum 
                    };
                    memcpy(data_region + datablock_no * BLOCK_SIZE, &self, sizeof(MFS_DirEnt_t));
                    memcpy(data_region + datablock_no * BLOCK_SIZE + sizeof(MFS_DirEnt_t), &parent, sizeof(MFS_DirEnt_t));
                    msync(data_region + datablock_no * BLOCK_SIZE, sizeof(MFS_DirEnt_t) * 2, MS_SYNC);
                }
                else
                { // create a file
                    inode_table[*emptySlot].size = BLOCK_SIZE;
                    inode_table[*emptySlot].type = 0;
                    for(int i = 0; i < DIRECT_PTRS; i ++)
                        inode_table[*emptySlot].direct[i] = (unsigned) -1;
                }
                msync(inode_bitmap, superBlock->inode_bitmap_len * BLOCK_SIZE, MS_SYNC);
                msync(data_bitmap, superBlock->data_bitmap_len * BLOCK_SIZE, MS_SYNC);
                msync(inode_table + *emptySlot, sizeof(inode_t), MS_SYNC);
                respondToServer(reply_msg, 0, sd, &addr, rc);
            }
            else if (strcmp(msg, "MFS_Unlink") == 0)
            {
                
            }
            else if (strcmp(msg, "MFS_Shutdown") == 0)
            {
                msync(image, image_size, MS_SYNC);
                respondToServer(reply_msg, 0, sd, &addr, rc);
                exit(0);
            }
        }
    }

    return 0;
}
