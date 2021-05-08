#include "ext2.h"
#include "../memory.h"
#include "../string.h"
#include "../uart.h"

#define INODE_DIRECT_COUNT 12
#define INODE_SINGLE_INDIRECT 12
#define INODE_DOUBLE_INDIRECT 13
#define INODE_TRIPLE_INDIRECT 14

// ext2_load_superblock(generic_block_t*) -> ext2fs_superblock_t*
// Loads a superblock from a block device.
ext2fs_superblock_t* ext2_load_superblock(generic_block_t* block) {
    // Allocate the superblock in memory
    ext2fs_superblock_t* superblock = malloc(sizeof(ext2fs_superblock_t));

    // Read the superblock
    generic_block_read(block, superblock, 2, (sizeof(ext2fs_superblock_t) + SECTOR_SIZE - 1) / SECTOR_SIZE);

    // Check the magic number
    if (superblock->magic != 0xef53) {
        uart_puts("ext2 filesystem not found.\n");
        free(superblock);
        return (ext2fs_superblock_t*) 0;
    }

    // Debug info
    uart_printf("File system has 0x%x inodes.\n", superblock->inodes_count);
    uart_printf("File system has 0x%x blocks.\n", superblock->blocks_count);

    return superblock;
}

// ext2fs_load_block(generic_block_t*, ext2fs_superblock_t*, unsigned int, void*) -> void
// Loads a block from an ext2 file system.
void ext2fs_load_block(generic_block_t* block, ext2fs_superblock_t* superblock, unsigned int block_id, void* data) {
    // Allocate the block in memory
    unsigned long long block_size = 1024 << superblock->log_block_size;

    // Read the block
    unsigned long long sector = block_id * block_size / SECTOR_SIZE;
    unsigned long long sector_count = block_size / SECTOR_SIZE;
    generic_block_read(block, data, sector, sector_count);
}

// ext2_load_block_descriptor_table(generic_block_t*, ext2fs_superblock_t*) -> ext2fs_block_descriptor_t*
// Loads a descriptor table from an ext2 file system.
ext2fs_block_descriptor_t* ext2_load_block_descriptor_table(generic_block_t* block, ext2fs_superblock_t* superblock) {
    // Allocate the table
    unsigned int table_size = (superblock->blocks_count + superblock->blocks_per_group - 1) / superblock->blocks_per_group * sizeof(ext2fs_block_descriptor_t);
    unsigned int sector_count = (table_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    ext2fs_block_descriptor_t* block_descriptor_table = malloc(sector_count * SECTOR_SIZE);

    // Read the table
    generic_block_read(block, block_descriptor_table, (1024 << superblock->log_block_size) * (superblock->first_data_block + 1) / SECTOR_SIZE, sector_count);

    return block_descriptor_table;
}

// ext2_load_inode(ext2fs_mount_t*, unsigned int) -> ext2fs_inode_t*
// Loads a given inode from an ext2 file system.
ext2fs_inode_t* ext2_load_inode(ext2fs_mount_t* mount, unsigned int inode) {
    // Inodes are one indexed because uhhhhh idk whyyyyyyyyyy :(
    inode--;

    // Allocate the inode
    ext2fs_inode_t* root = malloc(mount->superblock->inode_size);

    // Load block to memory
    unsigned int group = inode / mount->superblock->inodes_per_group;
    unsigned int inode_index = inode % mount->superblock->inodes_per_group;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;
    unsigned long long block_id = mount->desc_table[group].inode_table + inode_index * mount->superblock->inode_size / block_size;
    void* buffer = malloc(block_size);
    ext2fs_load_block(mount->block, mount->superblock, block_id, buffer);

    // Copy
    memcpy(root, buffer + (mount->superblock->inode_size * inode_index) % block_size, mount->superblock->inode_size);

    // Deallocate the buffer
    free(buffer);
    return root;
}

// ext2_get_root_inode(generic_block_t*, ext2fs_superblock_t*, ext2fs_block_descriptor_t*) -> ext2fs_inode_t*
// Loads the root inode from an ext2 file system.
ext2fs_inode_t* ext2_get_root_inode(generic_block_t* block, ext2fs_superblock_t* superblock, ext2fs_block_descriptor_t* desc_table) {
    ext2fs_mount_t mount = {
        .block      = block,
        .superblock = superblock,
        .desc_table = desc_table
    };
    return ext2_load_inode(&mount, 2);
}

// ext2_mount(generic_block_t*) -> ext2fs_mount_t
// Mounts an ext2 file system from a generic block device.
ext2fs_mount_t ext2_mount(generic_block_t* block) {
    // Get superblock
    ext2fs_superblock_t* superblock = ext2_load_superblock(block);
    if (superblock == (void*) 0)
        return (ext2fs_mount_t) {
            .block      = (void*) 0,
            .superblock = (void*) 0,
            .desc_table = (void*) 0,
            .root_inode = (void*) 0
        };

    // Get descriptor table
    ext2fs_block_descriptor_t* desc_table = ext2_load_block_descriptor_table(block, superblock);
    if (desc_table == (void*) 0) {
        free(superblock);
        return (ext2fs_mount_t) {
            .block      = (void*) 0,
            .superblock = (void*) 0,
            .desc_table = (void*) 0,
            .root_inode = (void*) 0
        };
    }

    // Get root inode
    ext2fs_inode_t* root_inode = ext2_get_root_inode(block, superblock, desc_table);
    if (root_inode == (void*) 0) {
        free(superblock);
        free(desc_table);
        return (ext2fs_mount_t) {
            .block      = (void*) 0,
            .superblock = (void*) 0,
            .desc_table = (void*) 0,
            .root_inode = (void*) 0
        };
    }

    // TODO: Update metadata

    // Success!
    return (ext2fs_mount_t) {
        .block      = block,
        .superblock = superblock,
        .desc_table = desc_table,
        .root_inode = root_inode
    };
}

struct __attribute__((__packed__, aligned(1))) s_dir_listing {
    unsigned int inode;
    unsigned short rec_len;
    unsigned char name_len;
    unsigned char file_type;
    char name[];
};

unsigned int ext2_fetch_from_directory_helper(ext2fs_mount_t* mount, char* file, void* data, unsigned long long block_size, unsigned int block_id) {
    // Get directory listing from block
    ext2fs_load_block(mount->block, mount->superblock, block_id, data);
    struct s_dir_listing* p = data;
    struct s_dir_listing* end = (struct s_dir_listing*) (((void*) data) + block_size);

    // Iterate over directory listings
    for (; p < end; p = p->rec_len ? ((void*) p) + p->rec_len : p + 1) {
        // Compare string
        char name[p->name_len + 1];
        name[p->name_len] = 0;
        memcpy(name, p->name, p->name_len);
        if (!strcmp(name, file))
            return p->inode;
    }

    return 0;
}

// ext2_fetch_from_directory(ext2fs_mount_t*, ext2fs_inode_t*, char*) -> unsigned int
// Fetches an inode's index from a directory.
unsigned int ext2_fetch_from_directory(ext2fs_mount_t* mount, ext2fs_inode_t* dir, char* file) {
    if ((dir->mode & 0xf000) != INODE_FILE_DIR)
        return 0;

    unsigned long long block_size = 1024 << mount->superblock->log_block_size;

    // Direct blocks
    void* data = malloc(block_size);
    for (int i = 0; i < INODE_DIRECT_COUNT; i++) {
        unsigned int block_id = dir->block[i];

        unsigned int inode = ext2_fetch_from_directory_helper(mount, file, data, block_size, block_id);
        if (inode != 0) {
            free(data);
            return inode;
        }
    }

    // Singly indirect block
    void* indirect1 = (void*) 0;
    if (dir->block[INODE_SINGLE_INDIRECT] != 0) {
        indirect1 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, dir->block[INODE_SINGLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size); i++) {
            unsigned int inode = ext2_fetch_from_directory_helper(mount, file, data, block_size, *i);
            if (inode != 0) {
                free(data);
                free(indirect1);
                return inode;
            }
        }
    }

    // Doubly indirect block
    unsigned int* indirect2 = (void*) 0;
    if (dir->block[INODE_DOUBLE_INDIRECT] != 0) {
        indirect2 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, dir->block[INODE_DOUBLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size); i++) {
            ext2fs_load_block(mount->block, mount->superblock, *i, indirect2);
            for (unsigned int* j = indirect2; j < (unsigned int*) (indirect2 + block_size); j++) {
                unsigned int inode = ext2_fetch_from_directory_helper(mount, file, data, block_size, *j);
                if (inode != 0) {
                    free(data);
                    free(indirect1);
                    free(indirect2);
                    return inode;
                }
            }
        }
    }

    // Triply indirect block
    unsigned int* indirect3 = (void*) 0;
    if (dir->block[INODE_TRIPLE_INDIRECT] != 0) {
        indirect2 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, dir->block[INODE_TRIPLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size); i++) {
            ext2fs_load_block(mount->block, mount->superblock, *i, indirect2);
            for (unsigned int* j = indirect2; j < (unsigned int*) (indirect2 + block_size); j++) {
                ext2fs_load_block(mount->block, mount->superblock, *j, indirect3);
                for (unsigned int* k = indirect3; k < (unsigned int*) (indirect3 + block_size); k++) {
                    unsigned int inode = ext2_fetch_from_directory_helper(mount, file, data, block_size, *k);
                    if (inode != 0) {
                        free(data);
                        free(indirect1);
                        free(indirect2);
                        return inode;
                    }
                }
            }
        }
    }

    // Free everything
    free(data);
    free(indirect1);
    free(indirect2);
    free(indirect3);
    return 0;
}

// ext2_get_inode(ext2fs_mount_t*, ext2fs_inode_t*, char**, unsigned long long) -> unsigned int
// Gets an inode's index by walking the path from a root inode.
unsigned int ext2_get_inode(ext2fs_mount_t* mount, ext2fs_inode_t* root, char** path, unsigned long long path_node_count) {
    ext2fs_inode_t* node = root;
    for (unsigned long long i = 0; i < path_node_count; i++) {
        char* path_node = path[i];
        unsigned int inode = ext2_fetch_from_directory(mount, node, path_node);

        if (node != root && node != mount->root_inode)
            free(node);

        if (inode == 0)
            return 0;
        else if (i == path_node_count - 1)
            return inode;
        else {
            node = ext2_load_inode(mount, inode);
            if (node == (void*) 0)
                return 0;
        }
    }

    return 0;
}

// ext2_dump_inode_buffer(ext2fs_mount_t*, ext2fs_inode_t*, void*, unsigned long long) -> void
// Dumps a buffer from an inode into memory.
void ext2_dump_inode_buffer(ext2fs_mount_t* mount, ext2fs_inode_t* file, void* data, unsigned long long block) {
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;

    if (block * block_size > file->size)
        return;

    if (block < INODE_DIRECT_COUNT) {
        ext2fs_load_block(mount->block, mount->superblock, file->block[block], data);
    } else if (block < INODE_DIRECT_COUNT + block_size / 4) {
        unsigned int* indirect = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_SINGLE_INDIRECT], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block - INODE_DIRECT_COUNT], data);
        free(indirect);
    } else if (block < INODE_DIRECT_COUNT + block_size / 4 + block_size * block_size / 16) {
        unsigned int* indirect = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_DOUBLE_INDIRECT], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[(block - INODE_DIRECT_COUNT) / (block_size * 4)], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[(block - INODE_DIRECT_COUNT) % (block_size * 4)], data);
        free(indirect);
    } else {
        unsigned int* indirect = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_DOUBLE_INDIRECT], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[(block - INODE_DIRECT_COUNT) / (block_size * 4)], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[(block - INODE_DIRECT_COUNT) / (block_size * 4) / (block_size * 4)], data);
        ext2fs_load_block(mount->block, mount->superblock, indirect[(block - INODE_DIRECT_COUNT) % (block_size * 4)], data);
        free(indirect);
    }
}

// ext2_generic_file_read_char(generic_file_t*) -> int
// Wrapper function for reading a character from a file. Returns EOF when at end of file.
int ext2_generic_file_read_char(generic_file_t* file) {
    if (file == (void*) 0)
        return EOF;
    ext2fs_inode_t* inode = file->metadata_buffer;
    if (inode == (void*) 0)
        return EOF;

    // Return EOF if the current position is past the size of the file
    if (file->pos >= (inode->size | ((unsigned long long) inode->dir_acl) << 32))
        return EOF;

    // Get some useful metadata
    ext2fs_mount_t* mount = file->fs->mount;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;

    // Get character
    char* buffer = (char*) (file->buffers[file->current_buffer]);
    char c = buffer[file->buffer_pos];

    // Set up buffers for the next character
    file->pos++;
    file->buffer_pos++;
    if (file->buffer_pos >= block_size) {
        // TODO: get next buffer
        return EOF;
    }

    return c;
}

// generic_file_t ext2_create_generic_regular_file(generic_filesystem_t*) -> generic_file_t
// Creates a generic file wrapper from an inode.
generic_file_t ext2_create_generic_regular_file(generic_filesystem_t* fs, unsigned int inode_index) {
    generic_file_t file = {
        .type = GENERIC_FILE_TYPE_REGULAR,
        .fs = fs,
        .pos = 0,
        .current_buffer = 0,
        .buffer_pos = 0,
        .buffer_block_indices = { inode_index, 0 },
        .metadata_buffer = ext2_load_inode(fs->mount, inode_index),
        .buffers = { 0 },
        .written_buffers = { 0 }
    };

    // Failed to get inode
    if (file.metadata_buffer == (void*) 0)
        return file;

    // Inode does not represent a regular file
    ext2fs_inode_t* inode = file.metadata_buffer;
    if ((inode->mode & 0xf000) != INODE_FILE_REGULAR) {
        free(file.metadata_buffer);
        file.metadata_buffer = (void*) 0;
        return file;
    }

    // Check if inode has data
    if (inode->size == 0 && inode->file_acl == 0)
        return file;

    // Get first buffer
    ext2fs_mount_t* mount = fs->mount;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;
    void* buffer = malloc(block_size);
    file.buffer_block_indices[1] = inode->block[0];
    ext2fs_load_block(mount->block, mount->superblock, file.buffer_block_indices[1], buffer);
    file.buffers[0] = buffer;

    return file;
}

// ext2_create_generic_filesystem(ext2fs_mount_t*) -> generic_filesystem_t
// Creates a generic file system for an ext2 file system.
generic_filesystem_t ext2_create_generic_filesystem(ext2fs_mount_t* mount) {
    return (generic_filesystem_t) {
        .mount = mount,
        .read_char = ext2_generic_file_read_char
    };
}

