// here's tree
#ifndef DEVICE_TREE_H
#define DEVICE_TREE_H

typedef unsigned char be32_t[4];
typedef unsigned char be64_t[8];

typedef enum {
    FDT_BEGIN_NODE  = 0x01,
    FDT_END_NODE    = 0x02,
    FDT_PROP        = 0x03,
    FDT_NOP         = 0x04,
    FDT_END         = 0x09
} fdt_node_type_t;

typedef struct __attribute__((packed)) {
    be32_t magic;
    be32_t totalsize;
    be32_t off_dt_struct;
    be32_t off_dt_strings;
    be32_t off_mem_rsvmap;
    be32_t version;
    be32_t last_comp_version;
    be32_t boot_cpuid_phys;
    be32_t size_dt_strings;
    be32_t size_dt_struct;
} fdt_header_t;

struct fdt_reserve_entry {
    be64_t address;
    be64_t size;
};

typedef struct {
    fdt_header_t* header;
    struct fdt_reserve_entry* memory_reservation_block;
    void* structure_block;
    char* strings_block;
} fdt_t;

struct fdt_property {
    unsigned int len;
    char* key;
    char* data;
};

// be_to_le(unsigned long long, void*) -> unsigned long long
// Converts a big endian number into a little endian number.
unsigned long long be_to_le(unsigned long long size, void* be);

// verify_fdt(void*) -> fdt_t
// Verifies a fdt by checking its magic number.
fdt_t verify_fdt(void* fdt);

// dump_fdt(fdt_t*, void*) -> void
// Dumps an fdt to the UART.
void dump_fdt(fdt_t* fdt, void*);

// fdt_find(fdt_t*, char*, void*) -> void*
// Finds a device tree node with the given name. Returns null on failure.
void* fdt_find(fdt_t* fdt, char* name, void* last);

// fdt_path(fdt_t*, char*, void*) -> void*
// Finds a device tree node with the given path. Returns null on failure.
void* fdt_path(fdt_t* fdt, char* path, void* last);

// fdt_get_node_addr(void*) -> unsigned long long
// Gets the address after the @ sign in a device tree node.
unsigned long long fdt_get_node_addr(void* node);

// fdt_get_property(fdt_t*, void*, char*) -> struct fdt_property
// Gets a property from a device tree node.
struct fdt_property fdt_get_property(fdt_t* fdt, void* node, char* key);

#endif /* DEVICE_TREE_H */
