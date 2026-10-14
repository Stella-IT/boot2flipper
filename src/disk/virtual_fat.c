#include "virtual_fat.h"
#include "crc32.h"
#include "mbr.h"
#include "gpt.h"
#include "bios_boot.h"
#include <storage/storage.h>
#include <ctype.h>
#include "../log.h"

#define TAG           "VirtualFAT"
#define MAX_FILES     32
#define SD_CACHE_SIZE 4096 // SD read-ahead size, multiple of SECTOR_SIZE

#define FAT_ENTRY_BAD 0x0FFFFFF7

// VFAT Long Filename (LFN) support
#define LFN_ATTR 0x0F // LFN attribute (read-only + system + hidden + volume)
#define LFN_LAST 0x40 // Last LFN entry flag

// Hosts pick the FAT type from the cluster count, so it must stay FAT32
_Static_assert(DATA_CLUSTERS(PARTITION_SECTORS_MBR) >= 65525, "MBR partition too small for FAT32");
_Static_assert(DATA_CLUSTERS(PARTITION_SECTORS_GPT) >= 65525, "GPT partition too small for FAT32");
_Static_assert(sizeof(BPB_VOLUME_LABEL) == 12, "Boot sector label must be 11 characters");
_Static_assert(sizeof(VOLUME_LABEL) == 12, "Volume label must be 11 characters");
// Boot sector, FSInfo and their backups (sector 6, 7) are reserved sectors, and
// the count is a 16-bit BPB field
_Static_assert(RESERVED_SECTORS >= 32 && RESERVED_SECTORS <= 0xFFFF, "Bad reserved sector count");
// Fixed files must sit at the same LBA in every scheme (see RESERVED_SECTORS)
_Static_assert(
    FAT_SIZE_SECTORS(PARTITION_SECTORS_MBR) == FAT_SIZE_SECTORS(PARTITION_SECTORS_GPT),
    "FAT size differs between schemes");

struct VirtualFat {
    VirtualFatFile files[MAX_FILES];
    uint8_t file_count;
    uint32_t next_cluster;
    uint32_t used_clusters; // Clusters taken by files and directories (root excluded)
    uint32_t held_start; // Clusters of a fixed file being added, skipped by allocations
    uint32_t held_end;
    PartitionScheme partition_scheme;
    WriteMode write_mode;
    int8_t bios_kernel_index; // File loaded by the BIOS boot loader, -1 if unset
    int8_t bios_initrd_index;
    int8_t bios_cmdline_index;

    // SD read-ahead: one open file and one cache shared by all SD-backed files,
    // so memory stays fixed no matter how many files are added
    File* sd_file; // Open handle of sd_file_index, NULL if none
    int8_t sd_file_index;
    uint32_t sd_file_pos; // Current position of sd_file
    uint8_t* sd_cache; // SD_CACHE_SIZE bytes, allocated on first use
    int8_t sd_cache_index; // File held in sd_cache, -1 if empty
    uint32_t sd_cache_offset; // File offset of sd_cache[0]

    // WRITE_MODE_MEMORY: sectors written by the host
    uint8_t* write_cache; // WRITE_CACHE_SECTORS sectors, allocated with the mode
    uint32_t write_cache_lba[WRITE_CACHE_SECTORS];
    uint8_t write_cache_count;

    VirtualFatFileReadCallback read_callback;
    void* callback_context;
};

VirtualFat* virtual_fat_alloc(void) {
    VirtualFat* vfat = malloc(sizeof(VirtualFat));
    memset(vfat, 0, sizeof(VirtualFat));

    vfat->file_count = 0;
    vfat->partition_scheme = PARTITION_SCHEME_HYBRID; // Default: BIOS and UEFI
    vfat->write_mode = WRITE_MODE_BAD; // Default: hosts can't add files
    vfat->next_cluster = 3; // Cluster 2 is root directory, files start at cluster 3
    vfat->bios_kernel_index = -1;
    vfat->bios_initrd_index = -1;
    vfat->bios_cmdline_index = -1;
    vfat->sd_file_index = -1;
    vfat->sd_cache_index = -1;

    return vfat;
}

static uint32_t clusters_for_size(uint32_t size) {
    return (size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) / (SECTORS_PER_CLUSTER * SECTOR_SIZE);
}

static uint32_t file_clusters(const VirtualFatFile* file) {
    return file->is_directory ? 1 : clusters_for_size(file->size); // Directories take 1 cluster
}

// Take count contiguous clusters, skipping the ones of fixed files
static uint32_t allocate_clusters(VirtualFat* vfat, uint32_t count) {
    uint32_t start = vfat->next_cluster;
    bool moved = true;
    while(moved && count > 0) {
        moved = false;
        if(start < vfat->held_end && vfat->held_start < start + count) {
            start = vfat->held_end;
            moved = true;
        }
        for(uint8_t i = 0; i < vfat->file_count; i++) {
            VirtualFatFile* file = &vfat->files[i];
            uint32_t end = file->start_cluster + file_clusters(file);
            if(file->fixed && start < end && file->start_cluster < start + count) {
                start = end;
                moved = true;
            }
        }
    }
    if(start + count > vfat->next_cluster) vfat->next_cluster = start + count;
    vfat->used_clusters += count;
    return start;
}

// 8.3 name for a long name. A name starting with dots (e.g. ".config") can't be a
// short name, so the dots are dropped and "~1" marks it, like Windows does.
static void make_short_name(char* short_name, const char* name, bool split_extension) {
    memset(short_name, ' ', 11);

    bool lossy = false;
    while(*name == '.') {
        name++;
        lossy = true;
    }

    const char* dot = split_extension ? strrchr(name, '.') : NULL; // Use last dot
    size_t name_len = dot ? (size_t)(dot - name) : strlen(name);
    size_t max_len = lossy ? 6 : 8;
    if(name_len > max_len) name_len = max_len;

    // Convert name to uppercase
    for(size_t i = 0; i < name_len; i++) {
        short_name[i] = toupper((unsigned char)name[i]);
    }
    if(lossy) memcpy(&short_name[name_len], "~1", 2);

    if(dot && strlen(dot + 1) > 0) {
        size_t ext_len = strlen(dot + 1);
        if(ext_len > 3) ext_len = 3;
        // Convert extension to uppercase
        for(size_t i = 0; i < ext_len; i++) {
            short_name[8 + i] = toupper((unsigned char)dot[1 + i]);
        }
    }
}

// Lowercase flags in byte 12 of a directory entry, as Windows NT writes them
#define DIR_CASE_LOWER_BASE      0x08
#define DIR_CASE_LOWER_EXTENSION 0x10

// Case of the letters in name[0..length): 1 upper, 2 lower, 3 mixed, 0 none.
// Sets *valid to false if a character can't be in an 8.3 name.
static uint8_t short_name_case(const char* name, size_t length, bool* valid) {
    uint8_t found = 0;
    for(size_t i = 0; i < length; i++) {
        unsigned char ch = name[i];
        if(isupper(ch)) {
            found |= 1;
        } else if(islower(ch)) {
            found |= 2;
        } else if(!isdigit(ch) && !strchr("!#$%&'()-@^_`{}~", ch)) {
            *valid = false;
        }
    }
    return found;
}

// Names that fit 8.3 with one case per part need no LFN entries, which saves
// directory entries: the 8.3 name and its lowercase flags hold them.
static uint8_t short_name_flags(const char* name, size_t length) {
    const char* dot = memchr(name, '.', length);
    size_t base = dot ? (size_t)(dot - name) : length;
    size_t extension = dot ? length - base - 1 : 0;
    if(base == 0 || base > 8 || extension > 3 || (dot && extension == 0)) return 0;
    if(dot && memchr(dot + 1, '.', extension)) return 0;

    bool valid = true;
    uint8_t base_case = short_name_case(name, base, &valid);
    uint8_t extension_case = dot ? short_name_case(dot + 1, extension, &valid) : 0;
    if(!valid || base_case == 3 || extension_case == 3) return 0;
    return 1 + (base_case == 2 ? DIR_CASE_LOWER_BASE : 0) +
           (extension_case == 2 ? DIR_CASE_LOWER_EXTENSION : 0);
}

static void set_long_name(VirtualFatFile* file, const char* name, size_t length) {
    if(length >= sizeof(file->long_name)) length = sizeof(file->long_name) - 1;
    memcpy(file->long_name, name, length);
    file->long_name[length] = '\0';
    file->short_case = short_name_flags(name, length);
}

static void sd_file_close(VirtualFat* vfat) {
    if(vfat->sd_file != NULL) {
        storage_file_close(vfat->sd_file);
        storage_file_free(vfat->sd_file);
        vfat->sd_file = NULL;
    }
    vfat->sd_file_index = -1;
}

void virtual_fat_free(VirtualFat* vfat) {
    if(vfat == NULL) return;

    sd_file_close(vfat);
    free(vfat->sd_cache);
    free(vfat->write_cache);

    // Free file data
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        if(vfat->files[i].source_type == FILE_SOURCE_MEMORY && !vfat->files[i].static_data &&
           vfat->files[i].memory_data != NULL) {
            free((void*)vfat->files[i].memory_data);
        } else if(vfat->files[i].source_type == FILE_SOURCE_SD_CARD && vfat->files[i].sd_path != NULL) {
            furi_string_free(vfat->files[i].sd_path);
        }
    }

    free(vfat);
}

bool virtual_fat_add_file(
    VirtualFat* vfat,
    const char* filename,
    const uint8_t* data,
    uint32_t size) {
    if(vfat == NULL || vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add file: filesystem full");
        return false;
    }

    VirtualFatFile* file = &vfat->files[vfat->file_count];
    set_long_name(file, filename, strlen(filename));
    make_short_name(file->name, filename, true);

    // Copy file data
    file->memory_data = malloc(size);
    memcpy((void*)file->memory_data, data, size);
    file->size = size;
    file->source_type = FILE_SOURCE_MEMORY;
    file->is_directory = false;
    file->parent_index = -1; // Root directory
    file->start_cluster = allocate_clusters(vfat, clusters_for_size(size));

    vfat->file_count++;

    B2F_LOG_I(
        TAG,
        "Added file: %.11s, size: %lu, cluster: %lu",
        file->name,
        file->size,
        file->start_cluster);

    return true;
}

bool virtual_fat_add_text_file(VirtualFat* vfat, const char* filename, const char* text) {
    return virtual_fat_add_file(vfat, filename, (const uint8_t*)text, strlen(text));
}

bool virtual_fat_add_sd_file(
    Storage* storage,
    VirtualFat* vfat,
    const char* filename,
    const char* sd_path) {
    if(vfat == NULL || vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add SD file: filesystem full");
        return false;
    }

    // Open SD file to get size
    File* file = storage_file_alloc(storage);

    if(!storage_file_open(file, sd_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        B2F_LOG_E(TAG, "Cannot open SD file: %s", sd_path);
        storage_file_free(file);
        return false;
    }

    uint64_t file_size = storage_file_size(file);
    storage_file_close(file);
    storage_file_free(file);

    VirtualFatFile* vfat_file = &vfat->files[vfat->file_count];
    set_long_name(vfat_file, filename, strlen(filename));
    make_short_name(vfat_file->name, filename, true);

    // Store SD path
    vfat_file->sd_path = furi_string_alloc_set(sd_path);
    vfat_file->size = (uint32_t)file_size;
    vfat_file->source_type = FILE_SOURCE_SD_CARD;
    vfat_file->is_directory = false;
    vfat_file->parent_index = -1; // Root directory
    vfat_file->start_cluster = allocate_clusters(vfat, clusters_for_size(vfat_file->size));

    vfat->file_count++;

    B2F_LOG_I(
        TAG,
        "Added SD file: %.11s, size: %lu, cluster: %lu, path: %s",
        vfat_file->name,
        vfat_file->size,
        vfat_file->start_cluster,
        sd_path);

    return true;
}

// FAT names are case-insensitive
static bool names_equal(const char* a, const char* b) {
    while(*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

// Helper: Find directory by name in parent
static int8_t find_directory(VirtualFat* vfat, const char* name, int8_t parent_index) {
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        if(vfat->files[i].is_directory && vfat->files[i].parent_index == parent_index &&
           names_equal(vfat->files[i].long_name, name)) {
            return i;
        }
    }
    return -1;
}

bool virtual_fat_add_directory(VirtualFat* vfat, const char* dirname) {
    if(vfat == NULL || vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add directory: filesystem full");
        return false;
    }

    VirtualFatFile* dir = &vfat->files[vfat->file_count];
    set_long_name(dir, dirname, strlen(dirname));
    make_short_name(dir->name, dirname, false);

    dir->size = 0;
    dir->source_type = FILE_SOURCE_MEMORY;
    dir->memory_data = NULL;
    dir->is_directory = true;
    dir->parent_index = -1; // Root directory
    dir->start_cluster = allocate_clusters(vfat, 1); // Directories take 1 cluster

    vfat->file_count++;

    B2F_LOG_I(TAG, "Added directory: %.11s, cluster: %lu", dir->name, dir->start_cluster);

    return true;
}

// Create nested directory path (e.g., "EFI/BOOT")
static int8_t create_directory_path(VirtualFat* vfat, const char* path) {
    if(path == NULL || strlen(path) == 0) return -1;

    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    int8_t current_parent = -1; // Start at root

    // Manual tokenization to avoid strtok (not available in Flipper API)
    char* ptr = path_copy;
    char token[64];

    while(*ptr != '\0') {
        // Skip leading slashes
        while(*ptr == '/')
            ptr++;
        if(*ptr == '\0') break;

        // Extract token until next slash or end
        size_t token_len = 0;
        while(*ptr != '\0' && *ptr != '/' && token_len < sizeof(token) - 1) {
            token[token_len++] = *ptr++;
        }
        token[token_len] = '\0';

        if(token_len == 0) break;

        // Check if directory already exists
        int8_t dir_index = find_directory(vfat, token, current_parent);

        if(dir_index < 0) {
            // Create new directory
            if(vfat->file_count >= MAX_FILES) {
                B2F_LOG_E(TAG, "Cannot create directory: filesystem full");
                return -1;
            }

            VirtualFatFile* dir = &vfat->files[vfat->file_count];
            set_long_name(dir, token, token_len);
            make_short_name(dir->name, token, false);

            dir->size = 0;
            dir->source_type = FILE_SOURCE_MEMORY;
            dir->memory_data = NULL;
            dir->is_directory = true;
            dir->parent_index = current_parent;
            dir->start_cluster = allocate_clusters(vfat, 1);

            dir_index = vfat->file_count;
            vfat->file_count++;

            B2F_LOG_I(
                TAG,
                "Created directory: %.11s (parent: %d, cluster: %lu)",
                dir->name,
                current_parent,
                dir->start_cluster);
        }

        current_parent = dir_index;
    }

    return current_parent;
}

bool virtual_fat_add_file_to_subdir(
    Storage* storage,
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    const char* sd_path) {
    if(vfat == NULL || vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add file to subdir: filesystem full");
        return false;
    }

    // Create parent directory path if needed
    int8_t parent_index = create_directory_path(vfat, parent_dir);
    if(parent_index < 0) {
        B2F_LOG_E(TAG, "Failed to create parent directory: %s", parent_dir);
        return false;
    }

    // Open SD file to get size
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, sd_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        B2F_LOG_E(TAG, "Cannot open SD file: %s", sd_path);
        storage_file_free(file);
        return false;
    }

    uint64_t file_size = storage_file_size(file);
    storage_file_close(file);
    storage_file_free(file);

    VirtualFatFile* vfat_file = &vfat->files[vfat->file_count];
    set_long_name(vfat_file, filename, strlen(filename));
    make_short_name(vfat_file->name, filename, true);

    vfat_file->sd_path = furi_string_alloc_set(sd_path);
    vfat_file->size = (uint32_t)file_size;
    vfat_file->source_type = FILE_SOURCE_SD_CARD;
    vfat_file->is_directory = false;
    vfat_file->parent_index = parent_index;
    vfat_file->start_cluster = allocate_clusters(vfat, clusters_for_size(vfat_file->size));

    vfat->file_count++;

    B2F_LOG_I(
        TAG,
        "Added file to subdir: %.11s, parent: %d, size: %lu, cluster: %lu",
        vfat_file->name,
        parent_index,
        vfat_file->size,
        vfat_file->start_cluster);

    return true;
}

void virtual_fat_set_allocation_start(VirtualFat* vfat, uint32_t cluster) {
    if(vfat == NULL) return;
    if(cluster > vfat->next_cluster) vfat->next_cluster = cluster;
}

bool virtual_fat_add_static_file(
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    const uint8_t* data,
    uint32_t size) {
    if(vfat == NULL || data == NULL) return false;

    // Parent first, so it is placed before the file
    int8_t parent_index = -1;
    if(parent_dir != NULL && parent_dir[0] != '\0') {
        parent_index = create_directory_path(vfat, parent_dir);
        if(parent_index < 0) {
            B2F_LOG_E(TAG, "Failed to create parent directory: %s", parent_dir);
            return false;
        }
    }
    if(vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add static file: filesystem full");
        return false;
    }

    VirtualFatFile* file = &vfat->files[vfat->file_count];
    memset(file, 0, sizeof(VirtualFatFile));
    set_long_name(file, filename, strlen(filename));
    make_short_name(file->name, filename, true);
    file->size = size;
    file->source_type = FILE_SOURCE_MEMORY;
    file->memory_data = data;
    file->static_data = true;
    file->parent_index = parent_index;
    file->start_cluster = allocate_clusters(vfat, clusters_for_size(size));
    vfat->file_count++;

    B2F_LOG_I(
        TAG,
        "Added static file: %.11s, parent: %d, size: %lu, cluster: %lu",
        file->name,
        parent_index,
        size,
        file->start_cluster);
    return true;
}

bool virtual_fat_add_directory_path(VirtualFat* vfat, const char* path, uint8_t attributes) {
    if(vfat == NULL) return false;

    int8_t dir_index = create_directory_path(vfat, path);
    if(dir_index < 0) {
        B2F_LOG_E(TAG, "Failed to create directory: %s", path);
        return false;
    }
    vfat->files[dir_index].attributes = attributes;
    return true;
}

// Check that clusters for a fixed file or directory are in range and free
static bool fixed_clusters_free(
    VirtualFat* vfat,
    const char* name,
    uint32_t start_cluster,
    uint32_t clusters) {
    // Root directory is cluster 2, and the clusters must be in the partition
    if(start_cluster < 3 || start_cluster + clusters > DATA_CLUSTERS(PARTITION_SECTORS_GPT) + 2) {
        B2F_LOG_E(TAG, "Fixed %s: cluster %lu out of range", name, start_cluster);
        return false;
    }
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        VirtualFatFile* other = &vfat->files[i];
        if(start_cluster < other->start_cluster + file_clusters(other) &&
           other->start_cluster < start_cluster + clusters) {
            B2F_LOG_E(TAG, "Fixed %s: cluster %lu is taken", name, start_cluster);
            return false;
        }
    }
    return true;
}

// Create the parents of a fixed entry, placed around the entry's clusters
static bool create_fixed_parent(
    VirtualFat* vfat,
    const char* parent_dir,
    uint32_t start_cluster,
    uint32_t clusters,
    int8_t* parent_index) {
    *parent_index = -1;
    if(parent_dir == NULL || parent_dir[0] == '\0') return true;

    vfat->held_start = start_cluster;
    vfat->held_end = start_cluster + clusters;
    *parent_index = create_directory_path(vfat, parent_dir);
    vfat->held_start = vfat->held_end = 0;
    if(*parent_index < 0) {
        B2F_LOG_E(TAG, "Failed to create parent directory: %s", parent_dir);
        return false;
    }
    return true;
}

bool virtual_fat_add_fixed_directory(
    VirtualFat* vfat,
    const char* path,
    uint32_t start_cluster,
    uint8_t attributes) {
    if(vfat == NULL || path == NULL) return false;
    if(vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add fixed directory: filesystem full");
        return false;
    }

    // Split "parent/name"
    const char* name = strrchr(path, '/');
    char parent_dir[256] = "";
    if(name != NULL) {
        size_t parent_len = name - path;
        if(parent_len >= sizeof(parent_dir)) parent_len = sizeof(parent_dir) - 1;
        memcpy(parent_dir, path, parent_len);
        parent_dir[parent_len] = '\0';
        name++;
    } else {
        name = path;
    }

    int8_t parent_index;
    if(!fixed_clusters_free(vfat, path, start_cluster, 1) ||
       !create_fixed_parent(vfat, parent_dir, start_cluster, 1, &parent_index)) {
        return false;
    }
    if(vfat->file_count >= MAX_FILES || find_directory(vfat, name, parent_index) >= 0) {
        B2F_LOG_E(TAG, "Cannot add fixed directory: %s", path);
        return false;
    }

    VirtualFatFile* dir = &vfat->files[vfat->file_count];
    memset(dir, 0, sizeof(VirtualFatFile));
    set_long_name(dir, name, strlen(name));
    make_short_name(dir->name, name, false);
    dir->source_type = FILE_SOURCE_MEMORY;
    dir->is_directory = true;
    dir->fixed = true;
    dir->attributes = attributes;
    dir->parent_index = parent_index;
    dir->start_cluster = start_cluster;
    vfat->file_count++;
    vfat->used_clusters++;

    B2F_LOG_I(TAG, "Added fixed directory: %.11s, cluster: %lu", dir->name, start_cluster);
    return true;
}

bool virtual_fat_add_fixed_file(
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    uint32_t start_cluster,
    uint8_t* buffer,
    uint32_t size,
    VirtualFatFileWriteCallback callback,
    void* context) {
    if(vfat == NULL || buffer == NULL || size == 0) return false;
    if(vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add fixed file: filesystem full");
        return false;
    }

    uint32_t clusters = clusters_for_size(size);
    int8_t parent_index;
    if(!fixed_clusters_free(vfat, filename, start_cluster, clusters) ||
       !create_fixed_parent(vfat, parent_dir, start_cluster, clusters, &parent_index)) {
        return false;
    }
    if(vfat->file_count >= MAX_FILES) {
        B2F_LOG_E(TAG, "Cannot add fixed file: filesystem full");
        return false;
    }

    VirtualFatFile* file = &vfat->files[vfat->file_count];
    memset(file, 0, sizeof(VirtualFatFile));
    set_long_name(file, filename, strlen(filename));
    make_short_name(file->name, filename, true);
    file->size = size;
    file->source_type = FILE_SOURCE_BUFFER;
    file->buffer = buffer;
    file->write_callback = callback;
    file->write_context = context;
    file->fixed = true;
    file->parent_index = parent_index;
    file->start_cluster = start_cluster;
    vfat->file_count++;
    vfat->used_clusters += clusters;

    B2F_LOG_I(
        TAG,
        "Added fixed file: %.11s, parent: %d, size: %lu, cluster: %lu",
        file->name,
        file->parent_index,
        file->size,
        file->start_cluster);

    return true;
}

static void generate_boot_sector(uint8_t* buffer, uint32_t total_sectors) {
    memset(buffer, 0, SECTOR_SIZE);

    /* clang-format off */

    // Jump instruction (3 bytes)
    buffer[0] = 0xEB;
    buffer[1] = 0x58;
    buffer[2] = 0x90;

    // OEM Name (8 bytes)
    memcpy(&buffer[3], "BOOT2FLP", 8);

    // BIOS Parameter Block (BPB)
    buffer[11] = SECTOR_SIZE & 0xFF;           // Bytes per sector (LSB)
    buffer[12] = (SECTOR_SIZE >> 8) & 0xFF;    // Bytes per sector (MSB)
    buffer[13] = SECTORS_PER_CLUSTER;          // Sectors per cluster
    buffer[14] = RESERVED_SECTORS & 0xFF;      // Reserved sectors (LSB)
    buffer[15] = (RESERVED_SECTORS >> 8) & 0xFF; // Reserved sectors (MSB)
    buffer[16] = FAT_COPIES;                   // Number of FATs
    buffer[17] = 0;                            // Root entries (0 for FAT32)
    buffer[18] = 0;
    buffer[19] = 0;                            // Small sectors (0 for FAT32)
    buffer[20] = 0;
    buffer[21] = 0xF8;                         // Media descriptor (removable disk)
    buffer[22] = 0;                            // FAT size (0 for FAT32)
    buffer[23] = 0;
    buffer[24] = 0x3F;                         // Sectors per track (63)
    buffer[25] = 0x00;
    buffer[26] = 0xFF;                         // Number of heads (255)
    buffer[27] = 0x00;
    buffer[28] = 0x00;                         // Hidden sectors
    buffer[29] = 0x00;
    buffer[30] = 0x00;
    buffer[31] = 0x00;

    // Total sectors (4 bytes)
    buffer[32] = total_sectors & 0xFF;
    buffer[33] = (total_sectors >> 8) & 0xFF;
    buffer[34] = (total_sectors >> 16) & 0xFF;
    buffer[35] = (total_sectors >> 24) & 0xFF;

    // Calculate FAT size
    uint32_t fat_size = FAT_SIZE_SECTORS(total_sectors);

    // FAT32 Extended BPB
    buffer[36] = fat_size & 0xFF;              // FAT size (4 bytes)
    buffer[37] = (fat_size >> 8) & 0xFF;
    buffer[38] = (fat_size >> 16) & 0xFF;
    buffer[39] = (fat_size >> 24) & 0xFF;
    buffer[40] = 0x00;                         // Extended flags
    buffer[41] = 0x00;
    buffer[42] = 0x00;                         // File system version
    buffer[43] = 0x00;
    buffer[44] = 0x02;                         // Root cluster (cluster 2)
    buffer[45] = 0x00;
    buffer[46] = 0x00;
    buffer[47] = 0x00;
    buffer[48] = 0x01;                         // FS Info sector (sector 1)
    buffer[49] = 0x00;
    buffer[50] = 0x06;                         // Backup boot sector (sector 6)
    buffer[51] = 0x00;
    // Bytes 52-63: Reserved (already zeroed)

    // Extended boot signature fields
    buffer[64] = 0x80;                         // Drive number (0x80 = hard disk)
    buffer[65] = 0x00;                         // Reserved
    buffer[66] = 0x29;                         // Extended boot signature
    buffer[67] = 0x12;                         // Volume serial (4 bytes)
    buffer[68] = 0x34;
    buffer[69] = 0x56;
    buffer[70] = 0x78;
    memcpy(&buffer[71], BPB_VOLUME_LABEL, 11); // Volume label (11 bytes)
    memcpy(&buffer[82], "FAT32   ", 8);       // File system type (8 bytes)

    // FAT32 Bootstrap code - only runs if something chainloads the partition.
    // BIOS boot is handled by the MBR boot code (see bios_boot.c), which
    // loads IPXE.LKR directly, so this just prints a message.
    const uint8_t fat32_bootstrap[] = {
        // Code starts at offset 90 (after BPB)
        0xFA,                           // CLI
        0x33, 0xC0,                     // XOR AX, AX
        0x8E, 0xD0,                     // MOV SS, AX
        0x8E, 0xD8,                     // MOV DS, AX
        0xBC, 0x00, 0x7C,               // MOV SP, 0x7C00
        0xFB,                           // STI

        // Print message: "iPXE boot - Use UEFI boot or embed iPXE"
        0xBE, 0x00, 0x01,               // MOV SI, message (offset 0x100 = 256)
        // Print loop
        0xAC,                           // LODSB
        0x08, 0xC0,                     // OR AL, AL
        0x74, 0x09,                     // JZ done
        0xB4, 0x0E,                     // MOV AH, 0x0E
        0xBB, 0x07, 0x00,               // MOV BX, 0x0007
        0xCD, 0x10,                     // INT 0x10
        0xEB, 0xF2,                     // JMP print_loop
        // Done - hang
        0xEB, 0xFE,                     // JMP $
    };

    /* clang-format on */

    memcpy(&buffer[90], fat32_bootstrap, sizeof(fat32_bootstrap));

    // Message at offset 256
    const char* message = "Use UEFI boot (BOOTX64.EFI) or embed iPXE in boot sector\r\n\0";
    memcpy(&buffer[256], message, strlen(message) + 1);

    // Boot signature (2 bytes)
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
}

static void
    generate_fsinfo_sector(uint8_t* buffer, uint32_t free_clusters, uint32_t next_free_cluster) {
    memset(buffer, 0, SECTOR_SIZE);

    // FS Info signature 1
    buffer[0] = 0x52; // "RRaA"
    buffer[1] = 0x52;
    buffer[2] = 0x61;
    buffer[3] = 0x41;

    // FS Info signature 2
    buffer[484] = 0x72; // "rrAa"
    buffer[485] = 0x72;
    buffer[486] = 0x41;
    buffer[487] = 0x61;

    // Free cluster count, so hosts don't scan the whole FAT to compute it
    buffer[488] = free_clusters & 0xFF;
    buffer[489] = (free_clusters >> 8) & 0xFF;
    buffer[490] = (free_clusters >> 16) & 0xFF;
    buffer[491] = (free_clusters >> 24) & 0xFF;

    // Next free cluster
    buffer[492] = next_free_cluster & 0xFF;
    buffer[493] = (next_free_cluster >> 8) & 0xFF;
    buffer[494] = (next_free_cluster >> 16) & 0xFF;
    buffer[495] = (next_free_cluster >> 24) & 0xFF;

    // Trail signature
    buffer[508] = 0x00;
    buffer[509] = 0x00;
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
}

static void generate_fat_sector(
    VirtualFat* vfat,
    uint32_t fat_sector,
    uint32_t data_clusters,
    uint8_t* buffer) {
    memset(buffer, 0, SECTOR_SIZE);

    uint32_t* fat = (uint32_t*)buffer;
    uint32_t entries_per_sector = SECTOR_SIZE / 4;
    uint32_t first_entry = fat_sector * entries_per_sector;

    // WRITE_MODE_BAD: every free cluster is bad, files below overwrite theirs
    if(vfat->write_mode == WRITE_MODE_BAD) {
        uint32_t from = MAX(first_entry, 3u); // Cluster 2 is the root directory
        uint32_t to = MIN(first_entry + entries_per_sector, data_clusters + 2);
        for(uint32_t cluster_num = from; cluster_num < to; cluster_num++) {
            fat[cluster_num - first_entry] = FAT_ENTRY_BAD;
        }
    }

    // First FAT sector has special entries
    if(fat_sector == 0) {
        fat[0] = 0x0FFFFFF8; // Media descriptor
        fat[1] = 0x0FFFFFFF; // End of chain marker
        fat[2] = 0x0FFFFFFF; // Root directory (cluster 2) - end of chain
    }

    // Build FAT chain for each file and directory
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        VirtualFatFile* file = &vfat->files[i];

        uint32_t clusters = file_clusters(file);
        if(clusters == 0) continue;

        // Only visit the part of the chain that falls in this FAT sector
        uint32_t last_cluster = file->start_cluster + clusters - 1;
        uint32_t from = MAX(file->start_cluster, first_entry);
        uint32_t to = MIN(last_cluster, first_entry + entries_per_sector - 1);
        if(from > to) continue;

        for(uint32_t cluster_num = from; cluster_num <= to; cluster_num++) {
            if(cluster_num == last_cluster) {
                fat[cluster_num - first_entry] = 0x0FFFFFFF; // End of chain
            } else {
                fat[cluster_num - first_entry] = cluster_num + 1; // Next cluster
            }
        }
    }
}

// Calculate LFN checksum for 8.3 name
static uint8_t lfn_checksum(const char* short_name) {
    uint8_t sum = 0;
    for(int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

// Generate one LFN entry (each holds 13 UTF-16 characters)
static void
    write_lfn_entry(uint8_t* entry, uint8_t sequence, const char* long_name, uint8_t checksum) {
    memset(entry, 0xFF, 32); // Fill with 0xFF (unused LFN chars)

    // Sequence number (bit 6 set for last entry)
    entry[0] = sequence;

    // Attribute (0x0F = LFN)
    entry[11] = LFN_ATTR;

    // Type (always 0 for LFN)
    entry[12] = 0x00;

    // Checksum
    entry[13] = checksum;

    // First cluster (always 0 for LFN)
    entry[26] = 0x00;
    entry[27] = 0x00;

    // Extract 13 characters for this entry (characters 0-4, 5-10, 11-12)
    uint8_t name_offset = ((sequence & 0x3F) - 1) * 13;
    uint8_t name_len = strlen(long_name);

    // Characters 1-5 (offset 1-10)
    for(int i = 0; i < 5; i++) {
        uint8_t idx = name_offset + i;
        uint16_t ch = (idx < name_len) ? (uint16_t)long_name[idx] : 0x0000;
        if(idx == name_len) ch = 0x0000; // Null terminator
        if(idx > name_len) ch = 0xFFFF; // Padding
        entry[1 + i * 2] = ch & 0xFF;
        entry[2 + i * 2] = (ch >> 8) & 0xFF;
    }

    // Characters 6-11 (offset 14-25)
    for(int i = 0; i < 6; i++) {
        uint8_t idx = name_offset + 5 + i;
        uint16_t ch = (idx < name_len) ? (uint16_t)long_name[idx] : 0x0000;
        if(idx == name_len) ch = 0x0000;
        if(idx > name_len) ch = 0xFFFF;
        entry[14 + i * 2] = ch & 0xFF;
        entry[15 + i * 2] = (ch >> 8) & 0xFF;
    }

    // Characters 12-13 (offset 28-31)
    for(int i = 0; i < 2; i++) {
        uint8_t idx = name_offset + 11 + i;
        uint16_t ch = (idx < name_len) ? (uint16_t)long_name[idx] : 0x0000;
        if(idx == name_len) ch = 0x0000;
        if(idx > name_len) ch = 0xFFFF;
        entry[28 + i * 2] = ch & 0xFF;
        entry[29 + i * 2] = (ch >> 8) & 0xFF;
    }
}

// Generate directory entry for a file/dir
static void write_directory_entry(
    uint8_t* entry,
    const char* name,
    uint8_t attributes,
    uint32_t start_cluster,
    uint32_t size) {
    // Filename (8.3)
    memcpy(entry, name, 11);

    // Attributes
    entry[11] = attributes;

    // Reserved
    memset(entry + 12, 0, 10);

    // Time (12:00:00)
    entry[22] = 0x00;
    entry[23] = 0x60;

    // Date (2024-01-01)
    entry[24] = 0x21;
    entry[25] = 0x58;

    // Start cluster (split across two locations in FAT32)
    entry[26] = start_cluster & 0xFF;
    entry[27] = (start_cluster >> 8) & 0xFF;
    entry[20] = (start_cluster >> 16) & 0xFF;
    entry[21] = (start_cluster >> 24) & 0xFF;

    // File size (0 for directories)
    entry[28] = size & 0xFF;
    entry[29] = (size >> 8) & 0xFF;
    entry[30] = (size >> 16) & 0xFF;
    entry[31] = (size >> 24) & 0xFF;
}

static void generate_root_directory(VirtualFat* vfat, uint8_t* buffer) {
    memset(buffer, 0, SECTOR_SIZE);

    uint8_t* entry = buffer;
    uint8_t entry_count = 0;

    B2F_LOG_I(TAG, "Generating root directory, file_count: %u", vfat->file_count);

    // Hosts show the volume label entry, not the one in the boot sector
    write_directory_entry(entry, VOLUME_LABEL, FAT_ATTR_VOLUME_ID, 0, 0);
    entry += 32;
    entry_count++;

    // Only show files/dirs with parent_index == -1 (root)
    for(uint8_t i = 0; i < vfat->file_count && entry_count < (SECTOR_SIZE / 32); i++) {
        VirtualFatFile* file = &vfat->files[i];

        if(file->parent_index != -1) {
            B2F_LOG_D(
                TAG, "Skipping file %u (not in root, parent_index=%d)", i, file->parent_index);
            continue; // Skip non-root entries
        }

        // Write LFN entries if long name exists
        if(file->long_name[0] != '\0' && !file->short_case) {
            uint8_t lfn_len = strlen(file->long_name);
            uint8_t lfn_entries = (lfn_len + 12) / 13; // Each LFN entry holds 13 chars
            uint8_t checksum = lfn_checksum(file->name);

            // Write LFN entries in reverse order
            for(int8_t j = lfn_entries; j >= 1 && entry_count < (SECTOR_SIZE / 32); j--) {
                uint8_t seq = j;
                if(j == lfn_entries) seq |= LFN_LAST; // Mark last entry
                write_lfn_entry(entry, seq, file->long_name, checksum);
                entry += 32;
                entry_count++;
            }
        }

        // Write 8.3 directory entry
        uint8_t attributes = (file->is_directory ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE) |
                             file->attributes;
        write_directory_entry(entry, file->name, attributes, file->start_cluster, file->size);
        if(file->short_case) entry[12] = file->short_case - 1;

        B2F_LOG_I(
            TAG,
            "Root entry %u: %.11s (LFN: %s), cluster: %lu, size: %lu, dir: %d",
            entry_count,
            file->name,
            file->long_name[0] ? file->long_name : "none",
            file->start_cluster,
            file->size,
            file->is_directory);

        entry += 32;
        entry_count++;
    }

    B2F_LOG_I(TAG, "Root directory complete, total entries: %u", entry_count);
}

// Generate subdirectory content (includes . and .. entries)
static void generate_subdirectory(VirtualFat* vfat, int8_t dir_index, uint8_t* buffer) {
    memset(buffer, 0, SECTOR_SIZE);

    if(dir_index < 0 || dir_index >= vfat->file_count) return;

    VirtualFatFile* dir = &vfat->files[dir_index];
    if(!dir->is_directory) return;

    uint8_t* entry = buffer;

    // . entry (self)
    char dot_name[11];
    memset(dot_name, ' ', 11);
    dot_name[0] = '.';
    write_directory_entry(entry, dot_name, 0x10, dir->start_cluster, 0);
    entry += 32;

    // .. entry (parent)
    char dotdot_name[11];
    memset(dotdot_name, ' ', 11);
    dotdot_name[0] = '.';
    dotdot_name[1] = '.';

    uint32_t parent_cluster = 2; // Default to root
    if(dir->parent_index >= 0) {
        parent_cluster = vfat->files[dir->parent_index].start_cluster;
    }
    write_directory_entry(entry, dotdot_name, 0x10, parent_cluster, 0);
    entry += 32;

    // Child entries
    uint8_t entry_count = 2; // Already have . and ..
    for(uint8_t i = 0; i < vfat->file_count && entry_count < (SECTOR_SIZE / 32); i++) {
        VirtualFatFile* file = &vfat->files[i];

        if(file->parent_index != dir_index) {
            continue; // Not a child of this directory
        }

        // Write LFN entries if long name exists
        if(file->long_name[0] != '\0' && !file->short_case) {
            uint8_t lfn_len = strlen(file->long_name);
            uint8_t lfn_entries = (lfn_len + 12) / 13;
            uint8_t checksum = lfn_checksum(file->name);

            for(int8_t j = lfn_entries; j >= 1 && entry_count < (SECTOR_SIZE / 32); j--) {
                uint8_t seq = j;
                if(j == lfn_entries) seq |= LFN_LAST;
                write_lfn_entry(entry, seq, file->long_name, checksum);
                entry += 32;
                entry_count++;
            }
        }

        // Write 8.3 directory entry
        uint8_t attributes = (file->is_directory ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE) |
                             file->attributes;
        write_directory_entry(entry, file->name, attributes, file->start_cluster, file->size);
        if(file->short_case) entry[12] = file->short_case - 1;

        entry += 32;
        entry_count++;
    }
}

// Read from an SD-backed file through the read-ahead cache. The last used file
// stays open, so sequential reads need no open/seek per sector.
static bool sd_file_read(
    Storage* storage,
    VirtualFat* vfat,
    int8_t index,
    uint32_t offset,
    uint8_t* buffer,
    uint32_t size) {
    VirtualFatFile* file = &vfat->files[index];
    uint32_t block = offset - (offset % SD_CACHE_SIZE);

    if(vfat->sd_cache_index != index || vfat->sd_cache_offset != block) {
        vfat->sd_cache_index = -1;
        if(vfat->sd_cache == NULL) vfat->sd_cache = malloc(SD_CACHE_SIZE);

        if(vfat->sd_file_index != index) {
            sd_file_close(vfat);
            vfat->sd_file = storage_file_alloc(storage);
            if(!storage_file_open(
                   vfat->sd_file,
                   furi_string_get_cstr(file->sd_path),
                   FSAM_READ,
                   FSOM_OPEN_EXISTING)) {
                B2F_LOG_E(TAG, "Failed to open SD file: %s", furi_string_get_cstr(file->sd_path));
                sd_file_close(vfat);
                return false;
            }
            vfat->sd_file_index = index;
            vfat->sd_file_pos = 0;
        }

        if(vfat->sd_file_pos != block) {
            if(!storage_file_seek(vfat->sd_file, block, true)) {
                B2F_LOG_E(TAG, "SD seek failed: %s @ %lu", file->long_name, block);
                sd_file_close(vfat);
                return false;
            }
            vfat->sd_file_pos = block;
        }

        uint32_t length = file->size - block;
        if(length > SD_CACHE_SIZE) length = SD_CACHE_SIZE;
        size_t bytes_read = storage_file_read(vfat->sd_file, vfat->sd_cache, length);
        if(bytes_read != length) {
            B2F_LOG_W(
                TAG, "SD read mismatch: expected %lu, got %u", length, (unsigned int)bytes_read);
            sd_file_close(vfat);
            return false;
        }
        vfat->sd_file_pos += length;
        vfat->sd_cache_index = index;
        vfat->sd_cache_offset = block;
    }

    memcpy(buffer, vfat->sd_cache + (offset - block), size);
    return true;
}

// Index of lba in the write cache, write_cache_count if not cached
static uint8_t write_cache_find(VirtualFat* vfat, uint32_t lba) {
    uint8_t i = 0;
    while(i < vfat->write_cache_count && vfat->write_cache_lba[i] != lba)
        i++;
    return i;
}

// Sector content as generated, without the host's writes. notify calls the read
// callback, which a comparison must not do.
static bool
    generate_sector(Storage* storage, VirtualFat* vfat, uint32_t lba, uint8_t* buffer, bool notify);

bool virtual_fat_read_sector(Storage* storage, VirtualFat* vfat, uint32_t lba, uint8_t* buffer) {
    if(vfat == NULL || buffer == NULL) return false;

    B2F_LOG_IO(TAG, "Reading LBA %lu", lba);

    // Sectors the host wrote in WRITE_MODE_MEMORY
    uint8_t cached = write_cache_find(vfat, lba);
    if(cached < vfat->write_cache_count) {
        memcpy(buffer, vfat->write_cache + cached * SECTOR_SIZE, SECTOR_SIZE);
        return true;
    }

    return generate_sector(storage, vfat, lba, buffer, true);
}

static bool
    generate_sector(Storage* storage, VirtualFat* vfat, uint32_t lba, uint8_t* buffer, bool notify) {
    // Partition size depends on scheme (use macros from virtual_fat.h)
    const bool gpt = vfat->partition_scheme != PARTITION_SCHEME_MBR_ONLY;
    const uint32_t PARTITION_SECTORS = gpt ? PARTITION_SECTORS_GPT : PARTITION_SECTORS_MBR;

    uint32_t fat_size = FAT_SIZE_SECTORS(PARTITION_SECTORS);
    uint32_t fat1_start = PARTITION_START + RESERVED_SECTORS;
    uint32_t fat2_start = fat1_start + fat_size;
    uint32_t data_start = fat2_start + fat_size;
    uint32_t free_clusters = DATA_CLUSTERS(PARTITION_SECTORS) - 1 - vfat->used_clusters;
    uint32_t next_free_cluster = vfat->next_cluster;
    if(vfat->write_mode == WRITE_MODE_BAD) {
        free_clusters = 0;
        next_free_cluster = 0xFFFFFFFF; // Unknown
    }

    // LBA 0: MBR or Protective MBR depending on partition scheme
    if(lba == 0) {
        if(vfat->partition_scheme == PARTITION_SCHEME_MBR_ONLY) {
            // MBR only - bootable FAT32 partition
            generate_mbr(buffer, PARTITION_START, PARTITION_SECTORS, 0xEF);
            B2F_LOG_D(TAG, "Generated MBR (MBR-only mode)");
        } else if(vfat->partition_scheme == PARTITION_SCHEME_HYBRID) {
            // GPT with an active MBR entry for the same partition
            generate_hybrid_mbr(buffer, PARTITION_START, PARTITION_SECTORS);
            B2F_LOG_D(TAG, "Generated hybrid MBR (Hybrid mode)");
        } else {
            // GPT only - protective MBR
            generate_protective_mbr(buffer, TOTAL_SECTORS);
            B2F_LOG_D(TAG, "Generated Protective MBR (GPT mode)");
        }
        // BIOS boot code in both modes (ignored by UEFI)
        bios_boot_write_mbr_code(buffer);
        return true;
    }

    // LBA 1: GPT Header (only in GPT mode)
    if(lba == 1) {
        if(gpt) {
            generate_gpt_header(buffer, TOTAL_SECTORS, PARTITION_START, PARTITION_SECTORS);
            B2F_LOG_D(TAG, "Generated GPT header at LBA 1");
        } else {
            // MBR mode: no GPT header
            memset(buffer, 0, SECTOR_SIZE);
        }
        return true;
    }

    // LBA 2: GPT Partition Entry Array (only in GPT mode)
    if(lba == 2) {
        if(gpt) {
            generate_gpt_partitions(buffer, PARTITION_START, PARTITION_SECTORS);
            B2F_LOG_D(TAG, "Generated GPT partitions at LBA 2");
        } else {
            // MBR mode: no GPT partitions
            memset(buffer, 0, SECTOR_SIZE);
        }
        return true;
    }

    // BIOS boot loader stage 2 (in the gap before the partition)
    if(bios_boot_is_stage2_sector(lba)) {
        BiosBootParams params = {0};
        if(vfat->bios_kernel_index >= 0) {
            VirtualFatFile* kernel = &vfat->files[vfat->bios_kernel_index];
            params.kernel_lba = data_start + (kernel->start_cluster - 2) * SECTORS_PER_CLUSTER;
            params.kernel_size = kernel->size;
        }
        if(vfat->bios_initrd_index >= 0) {
            VirtualFatFile* initrd = &vfat->files[vfat->bios_initrd_index];
            params.initrd_lba = data_start + (initrd->start_cluster - 2) * SECTORS_PER_CLUSTER;
            params.initrd_size = initrd->size;
        }
        if(vfat->bios_cmdline_index >= 0) {
            VirtualFatFile* cmdline = &vfat->files[vfat->bios_cmdline_index];
            params.cmdline_lba = data_start + (cmdline->start_cluster - 2) * SECTORS_PER_CLUSTER;
            params.cmdline_size = cmdline->size;
        }
        bios_boot_read_stage2_sector(lba, &params, buffer);
        return true;
    }

    // Empty sectors between GPT and partition (LBA 3 to PARTITION_START-1)
    if(lba > 2 && lba < PARTITION_START) {
        memset(buffer, 0, SECTOR_SIZE);
        return true;
    }

    // Backup GPT structures (only in GPT mode)
    if(gpt) {
        // Backup GPT partition array: starts at GPT_BACKUP_ARRAY_START
        if(lba >= GPT_BACKUP_ARRAY_START && lba < GPT_BACKUP_HEADER) {
            // Only first sector of backup partition array has data (like primary)
            if(lba == GPT_BACKUP_ARRAY_START) {
                generate_gpt_backup_partitions(buffer, PARTITION_START, PARTITION_SECTORS);
                B2F_LOG_D(TAG, "Generated backup GPT partitions at LBA %lu", lba);
            } else {
                memset(buffer, 0, SECTOR_SIZE);
            }
            return true;
        }

        // Backup GPT header: GPT_BACKUP_HEADER
        if(lba == GPT_BACKUP_HEADER) {
            generate_gpt_backup_header(buffer, TOTAL_SECTORS, PARTITION_START, PARTITION_SECTORS);
            B2F_LOG_D(TAG, "Generated backup GPT header at LBA %lu", lba);
            return true;
        }
    }

    // Boot sector at partition start
    if(lba == PARTITION_START) {
        generate_boot_sector(buffer, PARTITION_SECTORS);
        return true;
    }

    // FS Info sector at LBA 2
    if(lba == PARTITION_START + 1) {
        generate_fsinfo_sector(buffer, free_clusters, next_free_cluster);
        return true;
    }

    // Backup boot sector at LBA 7 (6 sectors after partition start)
    if(lba == PARTITION_START + 6) {
        generate_boot_sector(buffer, PARTITION_SECTORS);
        return true;
    }

    // Backup FS Info sector at LBA 8
    if(lba == PARTITION_START + 7) {
        generate_fsinfo_sector(buffer, free_clusters, next_free_cluster);
        return true;
    }

    // Other reserved sectors (empty)
    if(lba < fat1_start) {
        memset(buffer, 0, SECTOR_SIZE);
        return true;
    }

    // FAT1
    if(lba >= fat1_start && lba < fat2_start) {
        generate_fat_sector(vfat, lba - fat1_start, DATA_CLUSTERS(PARTITION_SECTORS), buffer);
        return true;
    }

    // FAT2 (copy of FAT1)
    if(lba >= fat2_start && lba < data_start) {
        generate_fat_sector(vfat, lba - fat2_start, DATA_CLUSTERS(PARTITION_SECTORS), buffer);
        return true;
    }

    // Data area
    if(lba >= data_start) {
        uint32_t cluster_offset = (lba - data_start) / SECTORS_PER_CLUSTER;
        uint32_t sector_in_cluster = (lba - data_start) % SECTORS_PER_CLUSTER;
        uint32_t cluster_num = cluster_offset + 2; // FAT clusters start at 2

        // Root directory is cluster 2
        if(cluster_num == 2 && sector_in_cluster == 0) {
            generate_root_directory(vfat, buffer);
            return true;
        }

        // Find which file/directory this cluster belongs to
        for(uint8_t i = 0; i < vfat->file_count; i++) {
            VirtualFatFile* file = &vfat->files[i];

            // Check if this is a directory cluster
            if(file->is_directory && cluster_num == file->start_cluster &&
               sector_in_cluster == 0) {
                generate_subdirectory(vfat, i, buffer);
                return true;
            }

            // For regular files, check cluster range
            if(!file->is_directory) {
                if(cluster_num >= file->start_cluster &&
                   cluster_num < file->start_cluster + file_clusters(file)) {
                    // This sector belongs to this file
                    uint32_t file_cluster = cluster_num - file->start_cluster;
                    uint32_t file_sector = file_cluster * SECTORS_PER_CLUSTER + sector_in_cluster;
                    uint32_t offset = file_sector * SECTOR_SIZE;

                    B2F_LOG_IO(
                        TAG,
                        "Reading file %.11s at cluster %lu, offset %lu, size %lu",
                        file->name,
                        cluster_num,
                        offset,
                        file->size);

                    // Trigger callback if set (only on first sector of file to avoid spam)
                    if(notify && vfat->read_callback && offset == 0) {
                        const char* display_name = file->long_name[0] != '\0' ? file->long_name :
                                                                                NULL;
                        if(display_name == NULL) {
                            // Fallback to 8.3 name
                            static char short_name_buf[13];
                            snprintf(
                                short_name_buf,
                                sizeof(short_name_buf),
                                "%.8s.%.3s",
                                file->name,
                                file->name + 8);
                            display_name = short_name_buf;
                        }
                        vfat->read_callback(display_name, vfat->callback_context);
                    }

                    memset(buffer, 0, SECTOR_SIZE);

                    if(offset < file->size) {
                        uint32_t copy_size = file->size - offset;
                        if(copy_size > SECTOR_SIZE) copy_size = SECTOR_SIZE;

                        if(file->source_type == FILE_SOURCE_BUFFER) {
                            memcpy(buffer, file->buffer + offset, copy_size);
                        } else if(file->source_type == FILE_SOURCE_MEMORY) {
                            // Read from RAM
                            B2F_LOG_IO(
                                TAG,
                                "Reading %lu bytes from memory at offset %lu",
                                copy_size,
                                offset);
                            memcpy(buffer, file->memory_data + offset, copy_size);
                        } else if(file->source_type == FILE_SOURCE_SD_CARD) {
                            // Stream from SD card (zeros on failure)
                            sd_file_read(storage, vfat, i, offset, buffer, copy_size);
                        }
                    }

                    return true;
                }
            }
        }

        // Empty sector
        memset(buffer, 0, SECTOR_SIZE);
        return true;
    }

    return false;
}

bool virtual_fat_write_sector(
    Storage* storage,
    VirtualFat* vfat,
    uint32_t lba,
    const uint8_t* buffer) {
    if(vfat == NULL || buffer == NULL) return false;

    // Writable fixed files take writes in every mode
    if(lba >= DATA_START_LBA) {
        uint32_t cluster_num = LBA_TO_CLUSTER(lba);
        uint32_t sector_in_cluster = (lba - DATA_START_LBA) % SECTORS_PER_CLUSTER;

        for(uint8_t i = 0; i < vfat->file_count; i++) {
            VirtualFatFile* file = &vfat->files[i];
            if(file->source_type != FILE_SOURCE_BUFFER || file->write_callback == NULL ||
               cluster_num < file->start_cluster ||
               cluster_num >= file->start_cluster + file_clusters(file)) {
                continue;
            }

            uint32_t offset =
                ((cluster_num - file->start_cluster) * SECTORS_PER_CLUSTER + sector_in_cluster) *
                SECTOR_SIZE;
            if(offset >= file->size) return true;
            uint32_t size = file->size - offset;
            if(size > SECTOR_SIZE) size = SECTOR_SIZE;

            B2F_LOG_IO(TAG, "Writing file %.11s, offset %lu, size %lu", file->name, offset, size);
            memcpy(file->buffer + offset, buffer, size);
            file->write_callback(file->write_context, offset, size);
            return true;
        }
    }

    switch(vfat->write_mode) {
    case WRITE_MODE_MEMORY: {
        if(vfat->write_cache == NULL) return false;
        uint8_t i = write_cache_find(vfat, lba);

        // Hosts rewrite whole runs of FAT sectors, so only keep what changed. A
        // sector written back to its generated content frees its slot.
        uint8_t* generated = vfat->write_cache + WRITE_CACHE_SECTORS * SECTOR_SIZE;
        if(generate_sector(storage, vfat, lba, generated, false) &&
           memcmp(generated, buffer, SECTOR_SIZE) == 0) {
            if(i < vfat->write_cache_count) {
                vfat->write_cache_count--;
                vfat->write_cache_lba[i] = vfat->write_cache_lba[vfat->write_cache_count];
                memcpy(
                    vfat->write_cache + i * SECTOR_SIZE,
                    vfat->write_cache + vfat->write_cache_count * SECTOR_SIZE,
                    SECTOR_SIZE);
            }
            B2F_LOG_IO(TAG, "Unchanged write to LBA %lu", lba);
            return true;
        }

        if(i == vfat->write_cache_count) {
            if(i == WRITE_CACHE_SECTORS) {
                B2F_LOG_W(TAG, "Write cache full, refused write to LBA %lu", lba);
                return false;
            }
            vfat->write_cache_lba[i] = lba;
            vfat->write_cache_count++;
        }
        B2F_LOG_IO(TAG, "Cached write to LBA %lu", lba);
        memcpy(vfat->write_cache + i * SECTOR_SIZE, buffer, SECTOR_SIZE);
        return true;
    }
    case WRITE_MODE_BAD:
        B2F_LOG_IO(TAG, "Dropped write to LBA %lu", lba);
        return true;
    default:
        B2F_LOG_IO(TAG, "Refused write to LBA %lu (read-only)", lba);
        return false;
    }
}

uint32_t virtual_fat_get_total_sectors(VirtualFat* vfat) {
    UNUSED(vfat);
    return TOTAL_SECTORS;
}

void virtual_fat_set_partition_scheme(VirtualFat* vfat, PartitionScheme scheme) {
    if(vfat == NULL) return;
    vfat->partition_scheme = scheme;
    B2F_LOG_I(
        TAG,
        "Partition scheme set to: %s",
        scheme == PARTITION_SCHEME_MBR_ONLY ? "MBR" :
        scheme == PARTITION_SCHEME_GPT_ONLY ? "GPT" :
                                              "Hybrid");
}

void virtual_fat_set_write_mode(VirtualFat* vfat, WriteMode mode) {
    if(vfat == NULL) return;
    vfat->write_mode = mode;

    // The cache only exists in memory mode, so other modes cost no RAM. The extra
    // sector holds generated content to compare writes with.
    free(vfat->write_cache);
    vfat->write_cache =
        mode == WRITE_MODE_MEMORY ? malloc((WRITE_CACHE_SECTORS + 1) * SECTOR_SIZE) : NULL;
    vfat->write_cache_count = 0;

    B2F_LOG_I(
        TAG,
        "Write mode set to: %s",
        mode == WRITE_MODE_READ_ONLY ? "Read-only" :
        mode == WRITE_MODE_MEMORY    ? "Memory" :
                                       "Bad");
}

bool virtual_fat_is_write_protected(VirtualFat* vfat) {
    return vfat == NULL || vfat->write_mode == WRITE_MODE_READ_ONLY;
}

// Find a file by path (e.g., "EFI/BOOT/BOOTX64.EFI"), -1 if not found
static int8_t find_file(VirtualFat* vfat, const char* path) {
    int8_t parent_index = -1;
    const char* name = path;
    const char* slash;
    while((slash = strchr(name, '/')) != NULL) {
        char dirname[64];
        size_t length = slash - name;
        if(length >= sizeof(dirname)) return -1;
        memcpy(dirname, name, length);
        dirname[length] = '\0';
        parent_index = find_directory(vfat, dirname, parent_index);
        if(parent_index < 0) return -1;
        name = slash + 1;
    }

    for(uint8_t i = 0; i < vfat->file_count; i++) {
        VirtualFatFile* file = &vfat->files[i];
        if(!file->is_directory && file->parent_index == parent_index &&
           names_equal(file->long_name, name)) {
            return i;
        }
    }
    return -1;
}

bool virtual_fat_set_bios_boot_files(
    VirtualFat* vfat,
    const char* kernel_path,
    const char* initrd_path,
    const char* cmdline_path) {
    if(vfat == NULL) return false;

    vfat->bios_kernel_index = find_file(vfat, kernel_path);
    vfat->bios_initrd_index = initrd_path ? find_file(vfat, initrd_path) : -1;
    vfat->bios_cmdline_index = cmdline_path ? find_file(vfat, cmdline_path) : -1;

    if(vfat->bios_kernel_index < 0) {
        B2F_LOG_E(TAG, "BIOS boot kernel not found: %s", kernel_path);
        return false;
    }
    if(initrd_path && vfat->bios_initrd_index < 0) {
        B2F_LOG_E(TAG, "BIOS boot initrd not found: %s", initrd_path);
        return false;
    }
    if(cmdline_path && (vfat->bios_cmdline_index < 0 ||
                        vfat->files[vfat->bios_cmdline_index].size > BIOS_BOOT_CMDLINE_MAX)) {
        B2F_LOG_E(TAG, "BIOS boot command line missing or too long: %s", cmdline_path);
        vfat->bios_cmdline_index = -1;
        return false;
    }
    return true;
}

void virtual_fat_set_read_callback(
    VirtualFat* vfat,
    VirtualFatFileReadCallback callback,
    void* context) {
    if(vfat == NULL) return;
    vfat->read_callback = callback;
    vfat->callback_context = context;
}
