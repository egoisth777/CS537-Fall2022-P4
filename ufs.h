#ifndef __ufs_h__
#define __ufs_h__

#define UFS_DIRECTORY    (0)
#define UFS_REGULAR_FILE (1)

#define UFS_BLOCK_SIZE (4096)

#define DIRECT_PTRS (30) // no. of data pointers 

typedef struct { // 128bytes
    int type;   // MFS_DIRECTORY or MFS_REGULAR // Let's assume 0 = dir ~ 1 = file
    int size;   // bytes
    unsigned int direct[DIRECT_PTRS];
} inode_t;

typedef struct {
    char name[28];  // up to 28 bytes of name in directory (including \0)
    int  inum;      // inode number of entry (-1 means entry not used)
} dir_ent_t;

// presumed: block 0 is the super block
typedef struct __super {
    int inode_bitmap_addr; // block address
    int inode_bitmap_len;  // in blocks
    int data_bitmap_addr;  // block address
    int data_bitmap_len;   // in blocks
    int inode_region_addr; // block address
    int inode_region_len;  // in blocks
    int data_region_addr;  // block address
    int data_region_len;   // in blocks
} super_t;


#endif // __ufs_h__