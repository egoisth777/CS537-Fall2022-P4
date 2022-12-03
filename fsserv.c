#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "mfs.h"
#include "udp.h"
#include "ufs.h"

#define BUFFER_SIZE (1000)

int main(int argc, char const *argv[])
{
    // Get length of the argument
    int length = 0; 
    char const* curr = argv[0];
    while(curr != NULL)
    {
        length ++;
        curr = argv[length];
    }
    
    // server [portnum] [image], argument number has to be 3.
    assert(length == 3); 

    // Convert arguments to variables of port number and file image filename.
    int portnum = atoi(argv[1]);
    char const* fileImage = argv[2];

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
    int* buf = malloc(sizeof(int) * 1024); // assume int is 4-byte size
    ssize_t numRead = read(fileImageFileDescriptor, buf, 4096); // read first 4096 bytes (super block)
    super_t superBlock;

    if (numRead == 4096) {
        superBlock.inode_bitmap_addr = buf[0];
        superBlock.inode_bitmap_len = buf[1];
        superBlock.data_bitmap_addr = buf[2];
        superBlock.data_bitmap_len = buf[3];
        superBlock.inode_region_addr = buf[4];
        superBlock.inode_region_len = buf[5];
        superBlock.data_region_addr = buf[6];
        superBlock.data_region_len = buf[7];
    }

    free(buf);

    // Sanity check
    printf("superBlock info\n inode_bitmap_addr: %d\n inode_bitmap_len: %d\n data_bitmap_addr: %d\n data_bitmap_len: %d\n data_region_addr: %d\n data_region_len: %d\n",
     superBlock.inode_bitmap_addr, superBlock.inode_bitmap_len, superBlock.data_bitmap_addr, superBlock.data_bitmap_len, superBlock.data_region_addr, superBlock.data_region_len);

    // Start the server
    
    while (1) {
	struct sockaddr_in addr;
	char message[BUFFER_SIZE];
	printf("The Machine:: waiting...\n");
	int rc = UDP_Read(sd, &addr, message, BUFFER_SIZE);
	printf("The Machine:: read message [size:%d contents:(%s)]\n", rc, message);
	if (rc > 0) {
            char reply[BUFFER_SIZE];
            sprintf(reply, "goodbye world");
            rc = UDP_Write(sd, &addr, reply, BUFFER_SIZE);
	    printf("The Machine:: reply\n");
	} 
    }
    

    return 0;
}
