#include "ext2.h"
#include "../../lib/memory.h"
#include "../../lib/string.h"
#include "../console/console.h"

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
        console_puts("ext2 filesystem not found.\n");
        free(superblock);
        return (ext2fs_superblock_t*) 0;
    }

    // Debug info
    console_printf("File system has 0x%x inodes.\n", superblock->inodes_count);
    console_printf("File system has 0x%x blocks.\n", superblock->blocks_count);

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

struct __attribute__((__packed__, aligned(1))) s_ext2_dir_listing {
    unsigned int inode;
    unsigned short rec_len;
    unsigned char name_len;
    unsigned char file_type;
    char name[];
};

unsigned int ext2_fetch_from_directory_helper(ext2fs_mount_t* mount, char* file, void* data, unsigned long long block_size, unsigned int block_id) {
    // Get directory listing from block
    ext2fs_load_block(mount->block, mount->superblock, block_id, data);
    struct s_ext2_dir_listing* p = data;
    struct s_ext2_dir_listing* end = (struct s_ext2_dir_listing*) (((void*) data) + block_size);

    // Iterate over directory listings
    for (; p < end && p->rec_len != 0; p = ((void*) p) + p->rec_len) {
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

        if (node != root)
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

    unsigned long long pointer_count = block_size / 4;
    if (block < INODE_DIRECT_COUNT) {
        ext2fs_load_block(mount->block, mount->superblock, file->block[block], data);
    } else if (block < INODE_DIRECT_COUNT + pointer_count) {
        unsigned int* indirect = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_SINGLE_INDIRECT], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block - INODE_DIRECT_COUNT], data);
        free(indirect);
    } else if (block < INODE_DIRECT_COUNT + pointer_count + pointer_count * pointer_count) {
        unsigned int* indirect = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_DOUBLE_INDIRECT], indirect);
        block -= INODE_DIRECT_COUNT - pointer_count;
        ext2fs_load_block(mount->block, mount->superblock, indirect[block / pointer_count], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block % pointer_count], data);
        free(indirect);
    } else {
        unsigned int* indirect = malloc(block_size);
        block -= INODE_DIRECT_COUNT - pointer_count - pointer_count * pointer_count;
        ext2fs_load_block(mount->block, mount->superblock, file->block[INODE_TRIPLE_INDIRECT], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block / pointer_count / pointer_count], data);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block / pointer_count % pointer_count], indirect);
        ext2fs_load_block(mount->block, mount->superblock, indirect[block % pointer_count], data);
        free(indirect);
    }
}

// ext2_generic_file_size(generic_file_t*) -> unsigned long long
// Returns the file size of a file.
unsigned long long ext2_generic_file_size(generic_file_t* file) {
    switch (file->type) {
        case GENERIC_FILE_TYPE_REGULAR: {
            ext2fs_inode_t* inode = file->buffer->metadata_buffer;
            return (((unsigned long long) inode->dir_acl) << 32) | (unsigned long long) inode->size;
        }

        case GENERIC_FILE_TYPE_DIR: {
            ext2fs_inode_t* inode = file->buffer->metadata_buffer;
            return (unsigned long long) inode->size;
        }

        case GENERIC_FILE_TYPE_BLOCK:
        case GENERIC_FILE_TYPE_SPECIAL:
        case GENERIC_FILE_TYPE_UNKNOWN:
            return 0;
    }
}

// ext2_generic_file_read_char(generic_file_t*) -> int
// Wrapper function for reading a character from a file. Returns EOF when at end of file.
int ext2_generic_file_read_char(generic_file_t* file) {
    if (file == (void*) 0)
        return EOF;
    if (file->type != GENERIC_FILE_TYPE_REGULAR)
        return EOF;

    generic_file_buffer_t* b = file->buffer;
    ext2fs_inode_t* inode = b->metadata_buffer;
    if (inode == (void*) 0)
        return EOF;
    if (b->pos >= ext2_generic_file_size(file))
        return EOF;

    // Get some useful metadata
    ext2fs_mount_t* mount = file->fs->mount;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;

    // Get character
    char* buffer = (char*) (b->buffers[b->current_buffer]);
    char c = buffer[b->buffer_pos];

    // Set up buffers for the next character
    b->pos++;
    b->buffer_pos++;
    if (b->buffer_pos >= block_size) {
        // Update index
        unsigned long long next_index = b->buffer_indices[b->current_buffer] + 1;
        b->buffer_pos = 0;
        b->current_buffer++;
        if (b->current_buffer >= BUFFER_COUNT - 3)
            b->current_buffer = 0;

        if (b->buffers[b->current_buffer] == (void*) 0)
            b->buffers[b->current_buffer] = malloc(block_size);

        unsigned long long block = b->pos / block_size;
        unsigned long long block_id = 0;

        // Direct blocks
        if (block < INODE_DIRECT_COUNT)
            block_id = inode->block[block];

        // TODO: indirect blocks
        else return EOF;

        // Get block
        ext2fs_load_block(mount->block, mount->superblock, block_id, b->buffers[b->current_buffer]);

        // Set metadata stuff
        b->buffer_block_indices[b->current_buffer + 1] = block_id;
        b->buffer_indices[b->current_buffer] = next_index;
    }

    return c;
}

struct s_dir_entry ext2_generic_dir_lookup(generic_file_t* dir, char* name) {
    // Get inode index
    generic_dir_t d = *dir->dir;
    ext2fs_mount_t* mount = dir->fs->mount;
    unsigned int inode_index = ext2_fetch_from_directory(mount, d->buffer->metadata_buffer, name);
    if (inode_index == 0)
        return (struct s_dir_entry) { 0 };

    // Get inode
    ext2fs_inode_t* inode = ext2_load_inode(mount, inode_index);
    unsigned short t = inode->mode & 0xf000;
    generic_file_type_t file_type = t == INODE_FILE_DIR ? GENERIC_FILE_TYPE_DIR : t == INODE_FILE_REGULAR ? GENERIC_FILE_TYPE_REGULAR : GENERIC_FILE_TYPE_UNKNOWN;

    // Get first buffer
    void* buffer;
    if (inode->block[0] != 0) {
        buffer = malloc(1024 << mount->superblock->log_block_size);
        ext2fs_load_block(mount->block, mount->superblock, inode->block[0], buffer);
    }

    // Create generic file
    generic_file_buffer_t* b = malloc(sizeof(generic_file_buffer_t));
    *b = (generic_file_buffer_t) {
        .pos = 0,
        .current_buffer = 0,
        .buffer_pos = 0,
        .buffer_indices = { 0 },
        .buffer_block_indices = { inode_index, inode->block[0], 0 },
        .metadata_buffer = inode,
        .buffers = { buffer, 0 },
        .written_buffers = { 0 }
    };
    generic_file_t* file = malloc(sizeof(generic_file_t));
    *file = (generic_file_t) {
        .parent = dir,
        .type = file_type,
        .fs = dir->fs,
    };

    // Append entry
    struct s_dir_entry entry = {
        .name = strdup(name),
        .file = file
    };

    if (file_type == GENERIC_FILE_TYPE_DIR) {
        generic_dir_t* dir = init_generic_dir();
        generic_dir_t d2 = *dir;
        d2->buffer = b;
        file->dir = dir;
    } else {
        file->buffer = b;
    }
    entry.file = file;

    return entry;
}

// generic_file_t ext2_create_generic_regular_file(generic_filesystem_t*) -> generic_file_t
// Creates a generic file wrapper from an inode.
generic_file_t ext2_create_generic_regular_file(generic_filesystem_t* fs, unsigned int inode_index) {
    generic_file_t file = {
        .type = GENERIC_FILE_TYPE_REGULAR,
        .fs = fs,
        .buffer = malloc(sizeof(generic_file_buffer_t))
    };

    *file.buffer = (generic_file_buffer_t) {
        .pos = 0,
        .current_buffer = 0,
        .buffer_pos = 0,
        .buffer_indices = { [0 ... BUFFER_COUNT - 1] = -1 },
        .buffer_block_indices = { inode_index, 0 },
        .metadata_buffer = ext2_load_inode(fs->mount, inode_index),
        .buffers = { 0 },
        .written_buffers = { 0 }
    };

    // Failed to get inode
    if (file.buffer->metadata_buffer == (void*) 0)
        return file;

    // Inode does not represent a regular file
    ext2fs_inode_t* inode = file.buffer->metadata_buffer;
    if ((inode->mode & 0xf000) != INODE_FILE_REGULAR) {
        free(file.buffer->metadata_buffer);
        free(file.buffer);
        file.buffer = (void*) 0;
        return file;
    }

    // Check if inode has data
    if (inode->size == 0 && inode->file_acl == 0)
        return file;

    // Get first buffer
    ext2fs_mount_t* mount = fs->mount;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;
    void* buffer = malloc(block_size);
    file.buffer->buffer_indices[0] = 0;
    file.buffer->buffer_block_indices[1] = inode->block[0];
    ext2fs_load_block(mount->block, mount->superblock, file.buffer->buffer_block_indices[1], buffer);
    file.buffer->buffers[0] = buffer;
    return file;
}

// ext2_unmount(generic_filesystem_t*, generic_file_t*) -> char
// Unmounts an ext2 file system.
char ext2_unmount(generic_file_t* root) {
    ext2fs_mount_t* mount = root->fs->mount;
    free(mount->superblock);
    free(mount->desc_table);
    free(mount);
    // TODO: save state
    return 0;
}

// ext2_generic_file_seek(generic_file_t*, unsigned long long) -> void
// Seeks to a position in the file.
void ext2_generic_file_seek(generic_file_t* file, unsigned long long pos) {
    if (file->type != GENERIC_FILE_TYPE_REGULAR)
        return;

    ext2fs_mount_t* mount = file->fs->mount;
    unsigned long long block_size = 1024 << mount->superblock->log_block_size;
    unsigned long long buffer = pos / block_size;
    unsigned long long buffer_pos = pos % block_size;
    file->buffer->pos = pos;
    file->buffer->buffer_pos = buffer_pos;

    // Search through already loaded buffers
    for (int i = 0; i < BUFFER_COUNT - 3; i++) {
        if (file->buffer->buffer_indices[i] == buffer) {
            file->buffer->current_buffer = i;
            return;
        }
    }

    // Get new buffer
    ext2fs_inode_t* inode = file->buffer->metadata_buffer;
    file->buffer->current_buffer++;
    if (file->buffer->current_buffer >= BUFFER_COUNT - 3)
        file->buffer->current_buffer = 0;

    if (file->buffer->buffers[file->buffer->current_buffer] == (void*) 0)
        file->buffer->buffers[file->buffer->current_buffer] = malloc(block_size);

    unsigned long long block_id = 0;

    // Direct blocks
    if (buffer < INODE_DIRECT_COUNT)
        block_id = inode->block[buffer];

    // TODO: indirect blocks
    else return;

    // Get block
    ext2fs_load_block(mount->block, mount->superblock, block_id, file->buffer->buffers[file->buffer->current_buffer]);

    // Set metadata stuff
    file->buffer->buffer_block_indices[file->buffer->current_buffer + 1] = block_id;
    file->buffer->buffer_indices[file->buffer->current_buffer] = buffer;
}

struct s_dir_entry* dir_entry_list_append(struct s_dir_entry* entries, unsigned long long* size, unsigned long long* length, struct s_dir_entry entry) {
    if (*length + 1 >= *size) {
        *size <<= 1;
        entries = realloc(entries, *size * sizeof(struct s_dir_entry));
    }

    entries[*length] = entry;
    entries[*length + 1] = (struct s_dir_entry) {
        .name = (void*) 0,
        .file = (void*) 0
    };

    *length += 1;
    return entries;
}

struct s_dir_entry* ext2_generic_dir_list_helper(ext2fs_mount_t* mount, unsigned int block_id, void* data, unsigned long long block_size, struct s_dir_entry* entries, unsigned long long* size, unsigned long long* length) {
    // Get directory listing from block
    ext2fs_load_block(mount->block, mount->superblock, block_id, data);
    struct s_ext2_dir_listing* p = data;
    struct s_ext2_dir_listing* end = (struct s_ext2_dir_listing*) (((void*) data) + block_size);

    // Iterate over directory listings
    for (; p < end && p->rec_len != 0; p = ((void*) p) + p->rec_len) {
        if (p->inode == 0)
            continue;

        // Get string
        char* name = malloc(p->name_len + 1);
        name[p->name_len] = 0;
        memcpy(name, p->name, p->name_len);

        // TODO: Get metadata from inode
        struct s_dir_entry entry = {
            .name = name,
            .file = (void*) 0
        };

        entries = dir_entry_list_append(entries, size, length, entry);
    }

    return entries;
}

// ext2_generic_dir_list(generic_file_t*) -> struct s_dir_entry*
// Returns a listing of the files in a directory.
struct s_dir_entry* ext2_generic_dir_list(generic_file_t* dir) {
    unsigned long long length = 0;
    unsigned long long size = 8;
    struct s_dir_entry* entries = malloc(size * sizeof(struct s_dir_entry));
    ext2fs_mount_t* mount = dir->fs->mount;
    ext2fs_inode_t* inode = (*dir->dir)->buffer->metadata_buffer;

    unsigned long long block_size = 1024 << mount->superblock->log_block_size;

    // Direct blocks
    void* data = malloc(block_size);
    for (int i = 0; i < INODE_DIRECT_COUNT; i++) {
        unsigned int block_id = inode->block[i];

        entries = ext2_generic_dir_list_helper(mount, block_id, data, block_size, entries, &size, &length);
    }

    // Singly indirect block
    void* indirect1 = (void*) 0;
    if (inode->block[INODE_SINGLE_INDIRECT] != 0) {
        indirect1 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, inode->block[INODE_SINGLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size) && *i; i++) {
            entries = ext2_generic_dir_list_helper(mount, *i, data, block_size, entries, &size, &length);
        }
    }

    // Doubly indirect block
    unsigned int* indirect2 = (void*) 0;
    if (inode->block[INODE_DOUBLE_INDIRECT] != 0) {
        indirect2 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, inode->block[INODE_DOUBLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size) && *i; i++) {
            ext2fs_load_block(mount->block, mount->superblock, *i, indirect2);
            for (unsigned int* j = indirect2; j < (unsigned int*) (indirect2 + block_size) && *j; j++) {
                entries = ext2_generic_dir_list_helper(mount, *j, data, block_size, entries, &size, &length);
            }
        }
    }

    // Triply indirect block
    unsigned int* indirect3 = (void*) 0;
    if (inode->block[INODE_TRIPLE_INDIRECT] != 0) {
        indirect2 = malloc(block_size);
        ext2fs_load_block(mount->block, mount->superblock, inode->block[INODE_TRIPLE_INDIRECT], indirect1);

        for (unsigned int* i = indirect1; i < (unsigned int*) (indirect1 + block_size) && *i; i++) {
            ext2fs_load_block(mount->block, mount->superblock, *i, indirect2);
            for (unsigned int* j = indirect2; j < (unsigned int*) (indirect2 + block_size) && *j; j++) {
                ext2fs_load_block(mount->block, mount->superblock, *j, indirect3);
                for (unsigned int* k = indirect3; k < (unsigned int*) (indirect3 + block_size) && *k; k++) {
                    entries = ext2_generic_dir_list_helper(mount, *k, data, block_size, entries, &size, &length);
                }
            }
        }
    }

    // Free everything
    free(data);
    free(indirect1);
    free(indirect2);
    free(indirect3);
    return entries;
}

// ext2_mount(generic_block_t*, generic_file_t*) -> char
// Mounts an ext2 file system from a generic block device. Returns 0 on success.
char ext2_mount(generic_block_t* block, generic_file_t* root) {
    // Get superblock
    ext2fs_superblock_t* superblock = ext2_load_superblock(block);
    if (superblock == (void*) 0)
        return 1;

    // Get descriptor table
    ext2fs_block_descriptor_t* desc_table = ext2_load_block_descriptor_table(block, superblock);
    if (desc_table == (void*) 0) {
        free(superblock);
        return 1;
    }

    // Get root inode
    ext2fs_inode_t* root_inode = ext2_get_root_inode(block, superblock, desc_table);
    if (root_inode == (void*) 0) {
        free(superblock);
        free(desc_table);
        return 1;
    }

    // TODO: Update metadata

    // Create mount data structure
    ext2fs_mount_t* mount = malloc(sizeof(ext2fs_mount_t));
    *mount = (ext2fs_mount_t) {
        .block = block,
        .superblock = superblock,
        .desc_table = desc_table,
    };
    generic_filesystem_t* fs = malloc(sizeof(generic_filesystem_t));
    *fs = (generic_filesystem_t) {
        .rc = 1,
        .mount = mount,
        .unmount = ext2_unmount,
        .read_char = ext2_generic_file_read_char,
        .lookup = ext2_generic_dir_lookup,
        .seek = ext2_generic_file_seek,
        .size = ext2_generic_file_size,
        .list = ext2_generic_dir_list
    };

    // Get first buffer
    void* buffer = malloc(1024 << superblock->log_block_size);
    ext2fs_load_block(block, superblock, root_inode->block[0], buffer);

    // Create directory structure
    root->type = GENERIC_FILE_TYPE_DIR;
    root->fs = fs;
    if (root->dir == (void*) 0)
        root->dir = init_generic_dir();

    generic_file_buffer_t* b = (*root->dir)->buffer;
    if (b == (void*) 0) {
        b = malloc(sizeof(generic_file_buffer_t));
        *b = (generic_file_buffer_t) {
            .metadata_buffer = root_inode,
            0
        };
        (*root->dir)->buffer = b;
    }


    // Success!
    return 0;
}
