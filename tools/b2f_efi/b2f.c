// b2f.efi - write the Boot2Flipper .B2F files from iPXE (or the UEFI shell)
//
//   chain file:/.B2F/bin/b2f.efi status Booting kernel...\nPlease wait
//   chain file:/.B2F/bin/b2f.efi frame logo.bin
//   chain file:/.B2F/bin/b2f.efi clear
//
// status  Text shown on the Flipper, "\n" starts a new line
// frame   Shows a 1024 byte 128x64 frame (see README). The file is opened on
//         the device b2f.efi was loaded from, which under iPXE holds the images
//         it fetched, e.g. "imgfetch http://server/logo.bin logo.bin"
// clear   Clears the status and frame, the Flipper shows its own screen again
//
// The Flipper is found by the "B2F INFO" header at B2F_INFO_LBA on each disk,
// and the file LBAs are read from it. Freestanding, no EFI library needed: see
// build.sh.

#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;

#define EFI_SUCCESS           0
#define EFI_ERROR_BIT         0x8000000000000000ULL
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2)
#define EFI_NOT_FOUND         (EFI_ERROR_BIT | 14)
#define EFI_ERROR(status)     (((status) & EFI_ERROR_BIT) != 0)

#define B2F_INFO_LBA    0x2002
#define B2F_MAGIC       "B2F INFO"
#define B2F_SECTOR_SIZE 512
#define B2F_FRAME_SIZE  1024

typedef struct {
    uint32_t data1;
    uint16_t data2, data3;
    uint8_t data4[8];
} EFI_GUID;

typedef struct {
    uint64_t signature;
    uint32_t revision, header_size, crc32, reserved;
} EFI_TABLE_HEADER;

typedef struct SIMPLE_TEXT_OUTPUT SIMPLE_TEXT_OUTPUT;
struct SIMPLE_TEXT_OUTPUT {
    void* reset;
    EFI_STATUS(EFIAPI* output_string)(SIMPLE_TEXT_OUTPUT* self, const CHAR16* string);
};

typedef struct {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages, *get_memory_map;
    void* allocate_pool;
    EFI_STATUS(EFIAPI* free_pool)(void* buffer);
    void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event, *check_event;
    void *install_protocol_interface, *reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    EFI_STATUS(EFIAPI* handle_protocol)(EFI_HANDLE handle, const EFI_GUID* protocol, void** iface);
    void *reserved, *register_protocol_notify, *locate_handle, *locate_device_path;
    void *install_configuration_table, *load_image, *start_image, *exit, *unload_image;
    void *exit_boot_services, *get_next_monotonic_count, *stall, *set_watchdog_timer;
    void *connect_controller, *disconnect_controller, *open_protocol, *close_protocol;
    void *open_protocol_information, *protocols_per_handle;
    EFI_STATUS(EFIAPI* locate_handle_buffer)
    (int search_type,
     const EFI_GUID* protocol,
     void* search_key,
     uint64_t* count,
     EFI_HANDLE** handles);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER hdr;
    CHAR16* firmware_vendor;
    uint32_t firmware_revision;
    EFI_HANDLE console_in_handle;
    void* con_in;
    EFI_HANDLE console_out_handle;
    SIMPLE_TEXT_OUTPUT* con_out;
    EFI_HANDLE standard_error_handle;
    SIMPLE_TEXT_OUTPUT* std_err;
    void* runtime_services;
    EFI_BOOT_SERVICES* boot_services;
} EFI_SYSTEM_TABLE;

typedef struct {
    uint32_t revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE* system_table;
    EFI_HANDLE device_handle;
    void* file_path;
    void* reserved;
    uint32_t load_options_size;
    void* load_options;
} EFI_LOADED_IMAGE;

typedef struct {
    uint32_t media_id;
    BOOLEAN removable_media, media_present, logical_partition, read_only, write_caching;
    uint32_t block_size, io_align;
    uint64_t last_block;
} EFI_BLOCK_IO_MEDIA;

typedef struct EFI_BLOCK_IO EFI_BLOCK_IO;
struct EFI_BLOCK_IO {
    uint64_t revision;
    EFI_BLOCK_IO_MEDIA* media;
    void* reset;
    EFI_STATUS(EFIAPI* read_blocks)
    (EFI_BLOCK_IO* self, uint32_t media_id, uint64_t lba, uint64_t size, void* buffer);
    EFI_STATUS(EFIAPI* write_blocks)
    (EFI_BLOCK_IO* self, uint32_t media_id, uint64_t lba, uint64_t size, const void* buffer);
    EFI_STATUS(EFIAPI* flush_blocks)(EFI_BLOCK_IO* self);
};

typedef struct EFI_FILE EFI_FILE;
struct EFI_FILE {
    uint64_t revision;
    EFI_STATUS(EFIAPI* open)
    (EFI_FILE* self, EFI_FILE** file, const CHAR16* name, uint64_t mode, uint64_t attributes);
    EFI_STATUS(EFIAPI* close)(EFI_FILE* self);
    void* delete_file;
    EFI_STATUS(EFIAPI* read)(EFI_FILE* self, uint64_t* size, void* buffer);
};

typedef struct EFI_SIMPLE_FILE_SYSTEM EFI_SIMPLE_FILE_SYSTEM;
struct EFI_SIMPLE_FILE_SYSTEM {
    uint64_t revision;
    EFI_STATUS(EFIAPI* open_volume)(EFI_SIMPLE_FILE_SYSTEM* self, EFI_FILE** root);
};

static const EFI_GUID block_io_guid =
    {0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID loaded_image_guid =
    {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID simple_file_system_guid =
    {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

// The compiler may emit calls to these
void* memset(void* dest, int value, size_t size) {
    uint8_t* d = dest;
    while(size--)
        *d++ = (uint8_t)value;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t size) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while(size--)
        *d++ = *s++;
    return dest;
}

static EFI_SYSTEM_TABLE* st;

// Block I/O buffers, aligned for any IoAlign a disk asks for
static uint8_t sector[B2F_SECTOR_SIZE] __attribute__((aligned(512)));
static uint8_t frame[B2F_FRAME_SIZE] __attribute__((aligned(512)));

static void print(const char* text) {
    CHAR16 buffer[128];
    size_t length = 0;
    while(*text) {
        if(*text == '\n') buffer[length++] = '\r';
        buffer[length++] = (uint8_t)*text++;
        if(length >= 126 || *text == '\0') {
            buffer[length] = 0;
            st->con_out->output_string(st->con_out, buffer);
            length = 0;
        }
    }
}

static uint32_t get_u32(const uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

typedef struct {
    EFI_BLOCK_IO* block_io;
    uint32_t status_lba, frame_lba;
} B2fDisk;

// Read the .B2F info header, true if the disk is a Flipper
static int b2f_disk_probe(EFI_BLOCK_IO* block_io, B2fDisk* disk) {
    EFI_BLOCK_IO_MEDIA* media = block_io->media;
    if(!media->media_present || media->logical_partition || media->block_size != B2F_SECTOR_SIZE ||
       media->last_block < B2F_INFO_LBA) {
        return 0;
    }
    if(EFI_ERROR(block_io->read_blocks(
           block_io, media->media_id, B2F_INFO_LBA, B2F_SECTOR_SIZE, sector))) {
        return 0;
    }
    for(int i = 0; i < 8; i++) {
        if(sector[i] != (uint8_t)B2F_MAGIC[i]) return 0;
    }

    disk->block_io = block_io;
    disk->status_lba = disk->frame_lba = 0;
    uint16_t count = sector[10] | sector[11] << 8;
    for(uint16_t i = 0; i < count && 16 + (i + 1) * 32 <= B2F_SECTOR_SIZE; i++) {
        const uint8_t* entry = sector + 16 + i * 32;
        const char* name = (const char*)entry;
        if(name[0] == 's' && name[1] == 't' && name[2] == 'a' && name[3] == 't' &&
           name[4] == 'u' && name[5] == 's' && name[6] == '\0') {
            disk->status_lba = get_u32(entry + 16);
        } else if(
            name[0] == 'f' && name[1] == 'r' && name[2] == 'a' && name[3] == 'm' &&
            name[4] == 'e' && name[5] == '\0') {
            disk->frame_lba = get_u32(entry + 16);
        }
    }
    return disk->status_lba != 0 && disk->frame_lba != 0;
}

static int b2f_disk_find(B2fDisk* disk) {
    EFI_HANDLE* handles;
    uint64_t count;
    if(EFI_ERROR(st->boot_services->locate_handle_buffer(
           2 /* ByProtocol */, &block_io_guid, NULL, &count, &handles))) {
        return 0;
    }

    int found = 0;
    for(uint64_t i = 0; i < count && !found; i++) {
        EFI_BLOCK_IO* block_io;
        if(!EFI_ERROR(st->boot_services->handle_protocol(
               handles[i], &block_io_guid, (void**)&block_io))) {
            found = b2f_disk_probe(block_io, disk);
        }
    }
    st->boot_services->free_pool(handles);
    return found;
}

static EFI_STATUS b2f_disk_write(B2fDisk* disk, uint32_t lba, const void* data, uint64_t size) {
    EFI_BLOCK_IO* block_io = disk->block_io;
    EFI_STATUS status =
        block_io->write_blocks(block_io, block_io->media->media_id, lba, size, data);
    if(!EFI_ERROR(status)) status = block_io->flush_blocks(block_io);
    if(EFI_ERROR(status)) print("b2f: write failed\n");
    return status;
}

// Command line as ASCII; non-ASCII characters become '?'
static char cmdline[512];

static const char* skip_word(const char* p) {
    while(*p && *p != ' ')
        p++;
    while(*p == ' ')
        p++;
    return p;
}

static int word_is(const char* p, const char* word) {
    while(*word && *p == *word) {
        p++;
        word++;
    }
    return *word == '\0' && (*p == '\0' || *p == ' ');
}

static EFI_STATUS b2f_status(B2fDisk* disk, const char* text) {
    memset(sector, 0, sizeof(sector));
    size_t length = 0;
    while(*text && length < B2F_SECTOR_SIZE - 1) {
        if(text[0] == '\\' && text[1] == 'n') {
            sector[length++] = '\n';
            text += 2;
        } else {
            sector[length++] = (uint8_t)*text++;
        }
    }
    return b2f_disk_write(disk, disk->status_lba, sector, B2F_SECTOR_SIZE);
}

static EFI_STATUS b2f_frame(B2fDisk* disk, EFI_HANDLE device, const char* name) {
    CHAR16 path[128];
    size_t length = 0;
    while(name[length] && name[length] != ' ' && length < 127) {
        path[length] = name[length] == '/' ? '\\' : (uint8_t)name[length];
        length++;
    }
    path[length] = 0;

    EFI_SIMPLE_FILE_SYSTEM* fs;
    EFI_FILE *root, *file;
    if(EFI_ERROR(
           st->boot_services->handle_protocol(device, &simple_file_system_guid, (void**)&fs)) ||
       EFI_ERROR(fs->open_volume(fs, &root))) {
        print("b2f: no file system to read the frame from\n");
        return EFI_NOT_FOUND;
    }
    EFI_STATUS status = root->open(root, &file, path, 1 /* Read */, 0);
    root->close(root);
    if(EFI_ERROR(status)) {
        print("b2f: frame file not found\n");
        return status;
    }

    uint64_t size = B2F_FRAME_SIZE;
    status = file->read(file, &size, frame);
    file->close(file);
    if(EFI_ERROR(status) || size != B2F_FRAME_SIZE) {
        print("b2f: frame file must be 1024 bytes\n");
        return EFI_INVALID_PARAMETER;
    }
    return b2f_disk_write(disk, disk->frame_lba, frame, B2F_FRAME_SIZE);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* system_table) {
    st = system_table;

    EFI_LOADED_IMAGE* loaded_image;
    if(EFI_ERROR(
           st->boot_services->handle_protocol(image, &loaded_image_guid, (void**)&loaded_image))) {
        return EFI_NOT_FOUND;
    }

    // The first word is the image name, as iPXE and the UEFI shell pass it
    const CHAR16* options = loaded_image->load_options;
    size_t count = options ? loaded_image->load_options_size / sizeof(CHAR16) : 0;
    size_t length = 0;
    for(size_t i = 0; i < count && options[i] && length < sizeof(cmdline) - 1; i++) {
        cmdline[length++] = options[i] < 0x80 ? (char)options[i] : '?';
    }
    cmdline[length] = '\0';
    const char* args = skip_word(cmdline);

    if(!word_is(args, "status") && !word_is(args, "frame") && !word_is(args, "clear")) {
        print("usage: b2f.efi status <text> | frame <file> | clear\n");
        return EFI_INVALID_PARAMETER;
    }

    B2fDisk disk;
    if(!b2f_disk_find(&disk)) {
        print("b2f: Flipper disk not found\n");
        return EFI_NOT_FOUND;
    }

    if(word_is(args, "status")) return b2f_status(&disk, skip_word(args));
    if(word_is(args, "frame")) {
        return b2f_frame(&disk, loaded_image->device_handle, skip_word(args));
    }

    // clear
    memset(frame, 0, sizeof(frame));
    EFI_STATUS status = b2f_disk_write(&disk, disk.frame_lba, frame, B2F_FRAME_SIZE);
    return EFI_ERROR(status) ? status : b2f_status(&disk, "");
}
