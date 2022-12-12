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
int 
lookup(int pinum, char *name, inode_t *inode_table, char *data_region, int *inumPtr, int data_region_addr)
{
    if (inode_table[pinum].type == 1){ // file should not be passed
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

int 
rm_dir(int inum, inode_t *inode_table, char* data_region, char* data_bitmap, char* inode_bitmap)
{
    inode_t metadata = inode_table[inum];
    int size = metadata.size;
    if (size >= 2 * sizeof(dir_ent_t))
        return -1;
    for(int i = 0; i < DIRECT_PTRS; i ++)
    {
        unsigned int data_addr = metadata.direct[i];
        if (data_addr == (unsigned int ) -1)
            continue;
        set_bit_zero((unsigned int *)data_bitmap, data_addr);
    }
    set_bit_zero((unsigned int *)inode_bitmap, inum);
    return 0;
}

int 
rm_file(int inum, inode_t *inode_table, char* data_bitmap, char* inode_bitmap)
{
    inode_t metadata = inode_table[inum];
    for(int i = 0; i < DIRECT_PTRS; i ++)
    {
        unsigned int data_addr = metadata.direct[i];
        if (data_addr == (unsigned int ) -1)
            continue;
        set_bit_zero((unsigned int *)data_bitmap, data_addr);
    }
    set_bit_zero((unsigned int *)inode_bitmap, inum);

    return 0;
}

int 
test_create(int pinum, int type, char* name, inode_t* inode_table, char* data_region, super_t *superBlock, char* inode_bitmap, char* data_bitmap, int data_region_start)
{
    if (strlen(name) > 28)
    { // check if the pinum is valid
        printf("stage 1\n");
    }
    int inum;
    if (lookup(pinum, name, inode_table, data_region, &inum, superBlock->data_region_addr) == 1)
    {
        printf("stage 2\n");
    }
    
    inode_t metadata = inode_table[pinum]; // meta data of the file/dir to create
    if (metadata.type == 1)
    { // cannot create a file inside a file
        printf("stage 3\n");
    }

    int emptySlot;
    if (find_empty_set_bitmap((unsigned int *)inode_bitmap, superBlock->num_inodes, &emptySlot) == 0)
    { // allocation failure, not enough spot
        printf("stage 4\n");
    }
    if (type == 0)
    { // create a directory
        inode_table[emptySlot].size = 2 * sizeof(dir_ent_t);
        inode_table[emptySlot].type = 0;
        int emptySlot2;
        if (find_empty_set_bitmap((unsigned int *)data_bitmap, superBlock->num_data, &emptySlot2) == 0)
        { // allocation failure, not enough spot
            printf("stage 5\n");
        }
        inode_table[emptySlot].direct[0] = emptySlot2;
        int datablock_no = inode_table[emptySlot].direct[0];
        MFS_DirEnt_t self = {
            ".", emptySlot};
        MFS_DirEnt_t parent = {
            "..", pinum};
        memcpy(data_region + datablock_no * BLOCK_SIZE, &self, sizeof(MFS_DirEnt_t));
        memcpy(data_region + datablock_no * BLOCK_SIZE + sizeof(MFS_DirEnt_t), &parent, sizeof(MFS_DirEnt_t));
        msync(data_region + datablock_no * BLOCK_SIZE, sizeof(MFS_DirEnt_t) * 2, MS_SYNC);
    }else
    { // create a file
        printf("stage6 \n");
        inode_table[emptySlot].size = 0;
        inode_table[emptySlot].type = 1;
        for (int i = 0; i < DIRECT_PTRS; i++)
            inode_table[emptySlot].direct[i] = (unsigned)-1;
    }

    dir_ent_t temp = {.inum = emptySlot};
    memcpy((char*)&temp, name, 20);
    temp.name[19] = '\0';
    int parentSize = metadata.size;
    int blockNum = parentSize / BLOCK_SIZE;
    int blockOffset = parentSize % BLOCK_SIZE;

    int curr = metadata.direct[blockNum] - data_region_start;
    memcpy(data_region + BLOCK_SIZE * curr + blockOffset, &temp, sizeof(dir_ent_t));
    inode_table[pinum].size = inode_table[pinum].size + sizeof(dir_ent_t);

    msync(inode_bitmap, superBlock->inode_bitmap_len * BLOCK_SIZE, MS_SYNC);
    msync(data_bitmap, superBlock->data_bitmap_len * BLOCK_SIZE, MS_SYNC);
    msync(inode_table + emptySlot, sizeof(inode_t), MS_SYNC);
    printf("stage 7\n");
}

int main(int argc, char const *argv[])
{
    char* const fileImage = "test.img";

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

    int inum;
    test_create(0, 1, "test", inode_table, data_region, superBlock, inode_bitmap, data_bitmap, superBlock->data_region_addr);
    int res = lookup(0, "test", inode_table, data_region, &inum, superBlock->data_region_addr);
    printf("res: %d \n", res);
    // Start the server
    return 0;
}
