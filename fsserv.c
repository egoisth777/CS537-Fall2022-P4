#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <math.h>
#include "udp.h"
#include "ufs.h"
#include "message.h"

#define BLOCK_SIZE (4096)

// create a socket and bind it to a port on the current machine
// used to listen for incoming packets
int UDP_Open(int port)
{
    int fd;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror("socket");
        return 0;
    }

    // set up the bind
    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1)
    {
        perror("bind");
        close(fd);
        return -1;
    }

    return fd;
}

// fill sockaddr_in struct with proper goodies
int UDP_FillSockAddr(struct sockaddr_in *addr, char *hostname, int port)
{
    bzero(addr, sizeof(struct sockaddr_in));
    if (hostname == NULL)
    {
        return 0; // it's OK just to clear the address
    }

    addr->sin_family = AF_INET;   // host byte order
    addr->sin_port = htons(port); // short, network byte order

    struct in_addr *in_addr;
    struct hostent *host_entry;
    if ((host_entry = gethostbyname(hostname)) == NULL)
    {
        perror("gethostbyname");
        return -1;
    }
    in_addr = (struct in_addr *)host_entry->h_addr;
    addr->sin_addr = *in_addr;

    return 0;
}

int UDP_Write(int fd, struct sockaddr_in *addr, char *buffer, int n)
{
    int addr_len = sizeof(struct sockaddr_in);
    int rc = sendto(fd, buffer, n, 0, (struct sockaddr *)addr, addr_len);
    return rc;
}

int UDP_Read(int fd, struct sockaddr_in *addr, char *buffer, int n)
{
    int len = sizeof(struct sockaddr_in);
    int rc = recvfrom(fd, buffer, n, 0, (struct sockaddr *)addr, (socklen_t *)&len);
    // assert(len == sizeof(struct sockaddr_in));
    return rc;
}

int UDP_Close(int fd)
{
    return close(fd);
}

// define some helper functions
/**
 * @brief Get the bit given the pointer to the inode bitmap/data bitmap
 *
 * @param bitmap the pointer to the inode bitmap or data bitmap
 * @param position the inode number
 * @return unsigned int
 */
unsigned int
get_bit(unsigned int *bitmap, int position)
{
    int index = position / 32;
    int offset = 31 - (position % 32);
    return (bitmap[index] >> offset) & 0x1;
}

/**
 * @brief Set the bit given the pointer to the inode bitmap/data bitmap
 *
 * @param bitmap the pointer to the inode bitmap or data bitmap
 * @param position the inode number
 * @return void
 */
void set_bit(unsigned int *bitmap, int position)
{
    int index = position / 32;
    int offset = 31 - (position % 32);
    bitmap[index] |= 0x1 << offset;
}

/**
 * @brief Set the bit given the pointer to the inode bitmap/data bitmap to 0
 *
 * @param bitmap the pointer to the inode bitmap or data bitmap
 * @param position the inode number
 * @return void
 */
void set_bit_zero(unsigned int *bitmap, int position)
{
    int index = position / 32;
    int offset = 31 - (position % 32);
    bitmap[index] |= 0x0 << offset;
}

/**
 * @brief find an empty spot in the Inode/data Bitmap, allocate it. Success will return 1, failure will return 0
 *
 * @param bitmap the starting address of the Inode bitmap
 * @param size size of the bitmap
 * @param emptySlot index of the emptySlot number
 * @return int success : 1, failure 0
 */
int find_empty_set_bitmap(unsigned int *bitmap, int size, int *emptySlot)
{
    int allocated = 0;
    for (int i = 0; i < size; i++)
    {
        if (get_bit(bitmap, i) == 0)
        {                       // find empty slot
            set_bit(bitmap, i); // set the empty slot to 1
            allocated = 1;
            *emptySlot = i; // set the empty slot index
            break;
        }
    }
    return allocated;
}

/**
 * @brief Check if the inum is valid (>0 & <= inode_num) and it is allocated
 *
 * @param inum the inode number
 * @param numInode number of inodes
 * @param inode_bitmap the pointer to the start of the inode_bitmap
 * @return int 0: invalid | 1: valid
 */
int IsInoValid(int inum, int num_inodes, unsigned int *inode_bitmap)
{
    if (inum < 0 || inum >= num_inodes || !get_bit(inode_bitmap, inum))
    {
        return 0;
    }
    return 1;
}

void respondToServer(message reply, int replyNum, int sd, struct sockaddr_in *addr, int *rc)
{
    // sprintf(&(reply.msg), "%d", replyNum);
    reply.msg_code = replyNum;
    *rc = UDP_Write(sd, addr, (char *)&reply, sizeof(reply));
    printf("The Machine:: reply\n");
}

int findNoBlockAlloc(int offset, int nbytes)
{
    if (offset % BLOCK_SIZE == 0 && nbytes <= 4096)
        return 1;
    return offset / BLOCK_SIZE == (offset + nbytes) / BLOCK_SIZE ? 1 : 2;
}

/**
 * @brief look up in the folder whether if the file with the name is contained
 *
 * @param pinum
 * @param name
 * @param inode_table
 * @param data_region
 * @param inumPtr
 * @return int
 */
int lookup(int pinum, char *name, inode_t *inode_table, char *data_region, int *inumPtr, int data_region_addr)
{
    if (inode_table[pinum].type == 1)
    { // file should not be passed
        return 0;
    }
    inode_t parent = inode_table[pinum];
    int found = 0;
    int parentSize = parent.size;
    int currentChecked = 0;
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        if (parent.direct[i] == (unsigned int)(-1)) // the directory is not valid{}
            continue;
        int curr = parent.direct[i] - data_region_addr;
        char *namePosition = data_region + (BLOCK_SIZE * curr);
        char currName[28];
        if (currentChecked >= parentSize)
            break;
        for (int j = 0; j < BLOCK_SIZE / sizeof(dir_ent_t); j++)
        {
            memcpy(&currName, namePosition + (j * 32), 28);
            *inumPtr = *(int *)(namePosition + (j * 32) + 28);
            if (strcmp(currName, name) == 0 && *inumPtr != -1)
            {
                found = 1;
                break;
            }
            currentChecked += sizeof(dir_ent_t);
        }
        if (found == 1)
            break;
    }
    return found;
}

int rm_dir(int inum, inode_t *inode_table, char *data_region, char *data_bitmap, char *inode_bitmap)
{
    inode_t metadata = inode_table[inum];
    int size = metadata.size;
    if (size > 2 * sizeof(dir_ent_t))
        return -1;
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        unsigned int data_addr = metadata.direct[i];
        if (data_addr == (unsigned int)-1)
            continue;
        set_bit_zero((unsigned int *)data_bitmap, data_addr);
    }
    set_bit_zero((unsigned int *)inode_bitmap, inum);
    return 0;
}

int rm_file(int inum, inode_t *inode_table, char *data_bitmap, char *inode_bitmap)
{
    inode_t metadata = inode_table[inum];
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        unsigned int data_addr = metadata.direct[i];
        if (data_addr == (unsigned int)-1)
            continue;
        set_bit_zero((unsigned int *)data_bitmap, data_addr);
    }
    set_bit_zero((unsigned int *)inode_bitmap, inum);

    return 0;
}

/**
 * @brief create something
 *
 * @param pinum
 * @param type
 * @param name
 * @param inode_table
 * @param data_region
 * @param data_bitmap
 * @param inode_bitmap
 * @param superBlock
 * @return int
 */
int MFS_create(int pinum, int type, char *name, inode_t *inode_table, char *data_region, char *data_bitmap, char *inode_bitmap, super_t *superBlock)
{

    if (strlen(name) > 28)
    { // check if the pinum is valid
        return -1;
    }
    int inum;
    if (lookup(pinum, name, inode_table, data_region, &inum, superBlock->data_region_addr) == 1)
    {
        return 0;
    }

    inode_t metadata = inode_table[pinum]; // meta data of the file/dir to create
    if (metadata.type == 1)
    { // cannot create a file inside a file
        return -1;
    }

    int emptySlot;
    if (find_empty_set_bitmap((unsigned int *)inode_bitmap, superBlock->num_inodes, &emptySlot) == 0)
    { // allocation failure, not enough spot
        return -1;
    }
    if (type == 0)
    { // create a directory
        inode_table[emptySlot].size = 2 * sizeof(dir_ent_t);
        inode_table[emptySlot].type = 0;
        int emptySlot2;
        if (find_empty_set_bitmap((unsigned int *)data_bitmap, superBlock->num_data, &emptySlot2) == 0)
        { // allocation failure, not enough spot
            return -1;
        }
        inode_table[emptySlot].direct[0] = emptySlot2 + superBlock->data_region_addr;
        int datablock_no = emptySlot2;
        dir_ent_t self = {
            ".", emptySlot};
        dir_ent_t parent = {
            "..", pinum};
        memcpy(data_region + datablock_no * BLOCK_SIZE, &self, sizeof(dir_ent_t));
        memcpy(data_region + datablock_no * BLOCK_SIZE + sizeof(dir_ent_t), &parent, sizeof(dir_ent_t));
        msync(data_region + datablock_no * BLOCK_SIZE, sizeof(dir_ent_t) * 2, MS_SYNC);
    }
    else
    { // create a file
        inode_table[emptySlot].size = 0;
        inode_table[emptySlot].type = 1;
        for (int i = 0; i < DIRECT_PTRS; i++)
            inode_table[emptySlot].direct[i] = (unsigned)-1;
    }

    dir_ent_t temp = {.inum = emptySlot};
    memcpy((char *)&temp, name, 20);
    temp.name[19] = '\0';
    int parentSize = metadata.size;
    int blockNum = parentSize / BLOCK_SIZE;
    int blockOffset = parentSize % BLOCK_SIZE;

    int curr = metadata.direct[blockNum] - superBlock->data_region_addr;
    memcpy(data_region + BLOCK_SIZE * curr + blockOffset, &temp, sizeof(dir_ent_t));
    inode_table[pinum].size = inode_table[pinum].size + sizeof(dir_ent_t);

    msync(inode_bitmap, superBlock->inode_bitmap_len * BLOCK_SIZE, MS_SYNC);
    msync(data_bitmap, superBlock->data_bitmap_len * BLOCK_SIZE, MS_SYNC);
    msync(inode_table + emptySlot, sizeof(inode_t), MS_SYNC);
    return 0;
}

int MFS_read(int nbytes, int offset, int inum, inode_t *inode_table, void *image, super_t *superBlock, char *buffer)
{
    if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset + nbytes > BLOCK_SIZE * DIRECT_PTRS)
    {
        return -1;
    }
    inode_t metadata = inode_table[inum];
    if (metadata.type == 0)
    { // directory
        int dir_size = sizeof(dir_ent_t);
        if (offset % dir_size != 0 || (offset + nbytes) % dir_size != 0)
        {
            return -1;
        }
    }

    int no_block_to_read = findNoBlockAlloc(offset, nbytes); // how many blocks will be read
    unsigned int comparison = -1;
    int no_bytes_to_read_1 = no_block_to_read == 2 ? BLOCK_SIZE - offset : nbytes; // number of bytes to be read from first block

    int startAddrFirstBlockOffset = offset % BLOCK_SIZE;                              // startin addr of read in first block
    int no_bytes_to_read_2 = no_block_to_read == 1 ? 0 : nbytes - no_bytes_to_read_1; // number of bytes to be read from second block

    int locationFirstBlock = offset / BLOCK_SIZE;
    int locationFirstBlockNum = metadata.direct[locationFirstBlock];
    int locationSecondBlock = locationFirstBlock + 1; // prove this
    int locationSecondBlockNum = metadata.direct[locationSecondBlock];

    // Operation on First Block
    int firstBlockAllocated = locationFirstBlockNum == comparison ? 0 : 1;
    if (firstBlockAllocated == 0)
    {
        return -1;
    }

    char *startAddr = image + superBlock->data_region_addr * BLOCK_SIZE + BLOCK_SIZE * locationFirstBlockNum + startAddrFirstBlockOffset;
    // Read
    memcpy(buffer, startAddr, no_bytes_to_read_1);

    if (no_block_to_read == 2)
    {
        // Operation on Second Block
        int secondBlockAllocated = locationSecondBlockNum == comparison ? 0 : 1;
        if (secondBlockAllocated == 0)
        {
            return -1;
        }
        char *startAddr2 = image + superBlock->data_region_addr * BLOCK_SIZE + BLOCK_SIZE * locationSecondBlockNum;
        // Read
        memcpy(buffer + no_bytes_to_read_1, startAddr2, no_bytes_to_read_2);
    }
    return 0;
}

/**
 * @brief Wrapper for the MFS write function in the server side
 *
 * @param nbytes
 * @param offset
 * @param inum
 * @param inode_table
 * @param data_bitmap
 * @param inode_bitmap
 * @param buffer
 * @param superBlock
 * @param image
 * @return int
 */
int MFS_write(int nbytes, int offset, int inum, inode_t *inode_table, char *data_bitmap, char *inode_bitmap, char *buffer, super_t *superBlock, void *image)
{
    // precheck
    if (nbytes <= 0 || nbytes > BLOCK_SIZE || offset < 0 || offset + nbytes > BLOCK_SIZE * DIRECT_PTRS)
    {
        return -1;
    }
    // Check if it is a regular file
    inode_t metadata = inode_table[inum];
    if (metadata.type == 0)
    { // directory {}
        return -1;
    }

    int numBlockToWrite = findNoBlockAlloc(offset, nbytes); // Number of Data Block To Write: 1 or 2
    unsigned int comparison = -1;

    int numByteToWriteFirstBlock = numBlockToWrite == 2 ? BLOCK_SIZE - offset : nbytes;
    int startAddrFirstBlockOffset = offset % BLOCK_SIZE;
    int numByteToWriteSecondBlock = numBlockToWrite == 1 ? 0 : nbytes - numByteToWriteFirstBlock;

    int locationFirstBlock = offset / BLOCK_SIZE;
    int locationFirstBlockNum = metadata.direct[locationFirstBlock];
    int locationSecondBlock = locationFirstBlock + 1; // prove this
    int locationSecondBlockNum = metadata.direct[locationSecondBlock];

    // Operation on First Block
    int firstBlockAllocated = locationFirstBlockNum == comparison ? 0 : 1;
    int emptySlot;
    if (firstBlockAllocated == 0)
    {
        firstBlockAllocated = find_empty_set_bitmap((unsigned int *)data_bitmap, superBlock->num_data, &emptySlot);
        metadata.direct[locationFirstBlock] = emptySlot;
    }
    
    locationFirstBlockNum = metadata.direct[locationFirstBlock];

    if (firstBlockAllocated == 0)
    {
        return -1;
    }

    char *startAddr = image + superBlock->data_region_addr * BLOCK_SIZE + BLOCK_SIZE * locationFirstBlockNum + startAddrFirstBlockOffset;
    // Write to persistency file
    memcpy(startAddr, buffer, numByteToWriteFirstBlock);
    msync(startAddr, numByteToWriteFirstBlock, MS_SYNC);

    if (numBlockToWrite == 2)
    {
        // Operation on Second Block
        int secondBlockAllocated = locationSecondBlockNum == comparison ? 0 : 1;
        if (secondBlockAllocated == 0)
        {
            secondBlockAllocated = find_empty_set_bitmap((unsigned int *)data_bitmap, superBlock->num_data, &emptySlot);
            metadata.direct[locationSecondBlock] = emptySlot;
        }

        
        locationSecondBlockNum = metadata.direct[locationSecondBlock];

        if (secondBlockAllocated == 0)
        {
            return -1;
        }

        char *startAddr2 = image + superBlock->data_region_addr * BLOCK_SIZE + BLOCK_SIZE * locationSecondBlockNum;
        // Write to persistency file
        memcpy(startAddr2, buffer, numByteToWriteSecondBlock);
        msync(startAddr2, numByteToWriteSecondBlock, MS_SYNC);
    }

    // update the size accordingly
    metadata.size = (nbytes + offset) > metadata.size ? nbytes + offset : metadata.size;

    msync(inode_bitmap, superBlock->inode_bitmap_len * BLOCK_SIZE, MS_SYNC);
    msync(data_bitmap, superBlock->data_bitmap_len * BLOCK_SIZE, MS_SYNC);
    memcpy(inode_table + inum, &metadata, sizeof(inode_t));
    msync(inode_table, superBlock->num_inodes * BLOCK_SIZE, MS_SYNC);
    return 0;
}

int 
MFS_unlink(int pinum, char * name, char * data_region, super_t * superBlock, inode_t * inode_table, char * data_bitmap, char * inode_bitmap, void * image, int image_size){

    int inum;
    int res = -1;

    int found = lookup(pinum, name, inode_table, data_region, &inum, superBlock->data_region_addr);
    if (found == 0) // not found
    {
        return -1;
    }
    inode_t metadata = inode_table[inum];
    if (metadata.type == 1)
        res = rm_file(inum, inode_table, data_bitmap, inode_bitmap);
    else
        res = rm_dir(inum, inode_table, data_region, data_bitmap, inode_bitmap);
    if (res == -1)
        return res;
    // parent 删除 name
    inode_t parent = inode_table[pinum];
    found = 0;
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        unsigned int curr = parent.direct[i];
        if (curr == (unsigned int)(-1)) // the directory is not valid{}
            continue;
        char *namePosition = data_region + (BLOCK_SIZE * (curr - superBlock->data_region_addr)) ;
        char currName[28];
        for (int j = 0; j < BLOCK_SIZE / sizeof(dir_ent_t); j++)
        {
            memcpy(&currName, namePosition + (j * sizeof(dir_ent_t)), 28);
            if (strcmp(currName, name) == 0)
            {
                found = 1;
                *(int *)(namePosition + (j * sizeof(dir_ent_t)) + 28 * sizeof(char)) = -1;
                break;
            }
        }
        if (found == 1)
            break;
    }
    if (res != -1) {
        parent.size -= sizeof(dir_ent_t);
        memcpy(inode_table + pinum, &metadata, sizeof(dir_ent_t));
    }
    // hahahahaha
    msync(image, image_size, MS_SYNC);
    return res;
}

int 
MFS_stat(message * reply_msg_ptr, inode_t * inode_table, int inum){

    inode_t metadata = inode_table[inum];
    reply_msg_ptr->param1 = metadata.size; // size of the inode
    reply_msg_ptr->param2 = metadata.type; // type of the inode
    return 0;
}

int main(int argc, char const *argv[])
{
    printf("Hello From Server \n");
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
    int image_fd = open(fileImage, O_RDWR);
    struct stat sbuf;
    int rc = fstat(image_fd, &sbuf);
    assert(rc > -1);
    int image_size = (int)sbuf.st_size;
    void *image = mmap(NULL, image_size, PROT_READ | PROT_WRITE, MAP_SHARED, image_fd, 0);
    assert(image != MAP_FAILED);

    super_t *superBlock = (super_t *)image;

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
        int rc = UDP_Read(sd, &addr, (char *)&received_msg, sizeof(message));
        printf("The Machine:: read message [size:%d contents:(%s)]\n", rc, received_msg.msg);
        if (rc <= 0)
        {
            continue;
        }

        message reply_msg; // message to be replied to client

        char *msg = received_msg.msg;

        int param1 = received_msg.param1; // pinum/inum
        int param2 = received_msg.param2;
        int param3 = received_msg.param3;

        if (!IsInoValid(param1, numInode, (unsigned int *)inode_bitmap)) // check if the inum is valid, not then continue;
        {
            respondToServer(reply_msg, -1, sd, &addr, &rc);
            continue;
        }
        // process by case according to the msg field
        if (strcmp(msg, "MFS_Init") == 0) // Initialization
        {
            respondToServer(reply_msg, 0, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Lookup") == 0)
        {
            inode_t *metadata = inode_table + param1;
            if (metadata->type == 1)
                respondToServer(reply_msg, -1, sd, &addr, &rc); // cannot look up in a file
            else {
                int inum;
                int found = lookup(param1, received_msg.charParam, inode_table, data_region, &inum, superBlock->data_region_addr);
                found = found == 0 ? -1 : inum;
                
                respondToServer(reply_msg, found, sd, &addr, &rc);
            }
            
        }
        else if (strcmp(msg, "MFS_Stat") == 0)
        {
            int res = MFS_stat(&reply_msg, inode_table, param1);
            respondToServer(reply_msg, res, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Write") == 0)
        {
            char *buffer = received_msg.buf;
            int res = MFS_write(param3, param2, param1, inode_table, data_bitmap, inode_bitmap, buffer, superBlock, image);
            respondToServer(reply_msg, res, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Read") == 0)
        {
            char buffer[4096];
            int res = MFS_read(param3, param2, param1, inode_table, image, superBlock, buffer);
            memcpy(reply_msg.buf, buffer, BLOCK_SIZE);
            respondToServer(reply_msg, res, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Creat") == 0)
        {
            int res = MFS_create(param1, param2, received_msg.charParam, inode_table, data_region, data_bitmap, inode_bitmap, superBlock);
            respondToServer(reply_msg, res, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Unlink") == 0)
        {
            int pinum = param1;
            char *name = received_msg.charParam;
            int res = MFS_unlink(param1, received_msg.charParam, data_region, superBlock, inode_table, data_bitmap, inode_bitmap, image, image_size);
            respondToServer(reply_msg, res, sd, &addr, &rc);
        }
        else if (strcmp(msg, "MFS_Shutdown") == 0)
        {
            msync(image, image_size, MS_SYNC);
            respondToServer(reply_msg, 0, sd, &addr, &rc);
            exit(0);
        }
    }
    return 0;
}

