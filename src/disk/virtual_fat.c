#include "virtual_fat.h"
#include "crc32.h"
#include "mbr.h"
#include "gpt.h"
#include <storage/storage.h>
#include <ctype.h>

#define TAG       "VirtualFAT"
#define MAX_FILES 16

// VFAT Long Filename (LFN) support
#define LFN_ATTR 0x0F // LFN attribute (read-only + system + hidden + volume)
#define LFN_LAST 0x40 // Last LFN entry flag

struct VirtualFat {
    VirtualFatFile files[MAX_FILES];
    uint8_t file_count;
    uint32_t next_cluster;
    PartitionScheme partition_scheme;
    VirtualFatFileReadCallback read_callback;
    void* callback_context;
};

VirtualFat* virtual_fat_alloc(void) {
    VirtualFat* vfat = malloc(sizeof(VirtualFat));
    memset(vfat, 0, sizeof(VirtualFat));

    vfat->file_count = 0;
    vfat->partition_scheme = PARTITION_SCHEME_GPT_ONLY; // Default: GPT (UEFI)
    vfat->next_cluster = 3; // Cluster 2 is root directory, files start at cluster 3

    return vfat;
}

void virtual_fat_free(VirtualFat* vfat) {
    if(vfat == NULL) return;

    // Free file data
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        if(vfat->files[i].source_type == FILE_SOURCE_MEMORY &&
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
        FURI_LOG_E(TAG, "Cannot add file: filesystem full");
        return false;
    }

    VirtualFatFile* file = &vfat->files[vfat->file_count];

    // Store long filename
    size_t fn_len = strlen(filename);
    if(fn_len >= sizeof(file->long_name)) fn_len = sizeof(file->long_name) - 1;
    memcpy(file->long_name, filename, fn_len);
    file->long_name[fn_len] = '\0';

    // Parse filename to 8.3 format (for compatibility)
    memset(file->name, ' ', 11);

    const char* dot = strrchr(filename, '.'); // Use last dot
    size_t name_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if(name_len > 8) name_len = 8;

    // Convert name to uppercase
    for(size_t i = 0; i < name_len; i++) {
        file->name[i] = toupper(filename[i]);
    }

    if(dot && strlen(dot + 1) > 0) {
        size_t ext_len = strlen(dot + 1);
        if(ext_len > 3) ext_len = 3;
        // Convert extension to uppercase
        for(size_t i = 0; i < ext_len; i++) {
            file->name[8 + i] = toupper(dot[1 + i]);
        }
    }

    // Copy file data
    file->memory_data = malloc(size);
    memcpy((void*)file->memory_data, data, size);
    file->size = size;
    file->source_type = FILE_SOURCE_MEMORY;
    file->is_directory = false;
    file->parent_index = -1; // Root directory
    file->start_cluster = vfat->next_cluster;

    // Calculate clusters needed
    uint32_t clusters_needed =
        (size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) / (SECTORS_PER_CLUSTER * SECTOR_SIZE);
    vfat->next_cluster += clusters_needed;

    vfat->file_count++;

    FURI_LOG_I(
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
        FURI_LOG_E(TAG, "Cannot add SD file: filesystem full");
        return false;
    }

    // Open SD file to get size
    File* file = storage_file_alloc(storage);

    if(!storage_file_open(file, sd_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "Cannot open SD file: %s", sd_path);
        storage_file_free(file);
        return false;
    }

    uint64_t file_size = storage_file_size(file);
    storage_file_close(file);
    storage_file_free(file);

    VirtualFatFile* vfat_file = &vfat->files[vfat->file_count];

    // Store long filename
    size_t fn_len = strlen(filename);
    if(fn_len >= sizeof(vfat_file->long_name)) fn_len = sizeof(vfat_file->long_name) - 1;
    memcpy(vfat_file->long_name, filename, fn_len);
    vfat_file->long_name[fn_len] = '\0';

    // Parse filename to 8.3 format
    memset(vfat_file->name, ' ', 11);

    const char* dot = strrchr(filename, '.'); // Use last dot
    size_t name_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if(name_len > 8) name_len = 8;

    // Convert to uppercase
    for(size_t i = 0; i < name_len; i++) {
        vfat_file->name[i] = toupper(filename[i]);
    }

    if(dot && strlen(dot + 1) > 0) {
        size_t ext_len = strlen(dot + 1);
        if(ext_len > 3) ext_len = 3;
        for(size_t i = 0; i < ext_len; i++) {
            vfat_file->name[8 + i] = toupper(dot[1 + i]);
        }
    }

    // Store SD path
    vfat_file->sd_path = furi_string_alloc_set(sd_path);
    vfat_file->size = (uint32_t)file_size;
    vfat_file->source_type = FILE_SOURCE_SD_CARD;
    vfat_file->is_directory = false;
    vfat_file->parent_index = -1; // Root directory
    vfat_file->start_cluster = vfat->next_cluster;

    // Calculate clusters needed
    uint32_t clusters_needed = (vfat_file->size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) /
                               (SECTORS_PER_CLUSTER * SECTOR_SIZE);
    vfat->next_cluster += clusters_needed;

    vfat->file_count++;

    FURI_LOG_I(
        TAG,
        "Added SD file: %.11s, size: %lu, cluster: %lu, path: %s",
        vfat_file->name,
        vfat_file->size,
        vfat_file->start_cluster,
        sd_path);

    return true;
}

// Helper: Find directory by name in parent
static int8_t find_directory(VirtualFat* vfat, const char* name, int8_t parent_index) {
    char search_name[11];
    memset(search_name, ' ', 11);

    size_t name_len = strlen(name);
    if(name_len > 8) name_len = 8;

    for(size_t i = 0; i < name_len; i++) {
        search_name[i] = toupper(name[i]);
    }

    for(uint8_t i = 0; i < vfat->file_count; i++) {
        if(vfat->files[i].is_directory && vfat->files[i].parent_index == parent_index &&
           memcmp(vfat->files[i].name, search_name, 11) == 0) {
            return i;
        }
    }
    return -1;
}

bool virtual_fat_add_directory(VirtualFat* vfat, const char* dirname) {
    if(vfat == NULL || vfat->file_count >= MAX_FILES) {
        FURI_LOG_E(TAG, "Cannot add directory: filesystem full");
        return false;
    }

    VirtualFatFile* dir = &vfat->files[vfat->file_count];

    // Store long dirname
    size_t dn_len = strlen(dirname);
    if(dn_len >= sizeof(dir->long_name)) dn_len = sizeof(dir->long_name) - 1;
    memcpy(dir->long_name, dirname, dn_len);
    dir->long_name[dn_len] = '\0';

    // Parse dirname to 8.3 format
    memset(dir->name, ' ', 11);

    size_t name_len = strlen(dirname);
    if(name_len > 8) name_len = 8;

    // Convert to uppercase
    for(size_t i = 0; i < name_len; i++) {
        dir->name[i] = toupper(dirname[i]);
    }

    dir->size = 0;
    dir->source_type = FILE_SOURCE_MEMORY;
    dir->memory_data = NULL;
    dir->is_directory = true;
    dir->parent_index = -1; // Root directory
    dir->start_cluster = vfat->next_cluster;

    // Directories take 1 cluster
    vfat->next_cluster++;

    vfat->file_count++;

    FURI_LOG_I(TAG, "Added directory: %.11s, cluster: %lu", dir->name, dir->start_cluster);

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
                FURI_LOG_E(TAG, "Cannot create directory: filesystem full");
                return -1;
            }

            VirtualFatFile* dir = &vfat->files[vfat->file_count];

            // Store long name
            if(token_len >= sizeof(dir->long_name)) token_len = sizeof(dir->long_name) - 1;
            memcpy(dir->long_name, token, token_len);
            dir->long_name[token_len] = '\0';

            // Parse name to 8.3 format
            memset(dir->name, ' ', 11);
            size_t name_len = token_len;
            if(name_len > 8) name_len = 8;

            for(size_t i = 0; i < name_len; i++) {
                dir->name[i] = toupper(token[i]);
            }

            dir->size = 0;
            dir->source_type = FILE_SOURCE_MEMORY;
            dir->memory_data = NULL;
            dir->is_directory = true;
            dir->parent_index = current_parent;
            dir->start_cluster = vfat->next_cluster;

            vfat->next_cluster++;
            dir_index = vfat->file_count;
            vfat->file_count++;

            FURI_LOG_I(
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
        FURI_LOG_E(TAG, "Cannot add file to subdir: filesystem full");
        return false;
    }

    // Create parent directory path if needed
    int8_t parent_index = create_directory_path(vfat, parent_dir);
    if(parent_index < 0) {
        FURI_LOG_E(TAG, "Failed to create parent directory: %s", parent_dir);
        return false;
    }

    // Open SD file to get size
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, sd_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "Cannot open SD file: %s", sd_path);
        storage_file_free(file);
        return false;
    }

    uint64_t file_size = storage_file_size(file);
    storage_file_close(file);
    storage_file_free(file);

    VirtualFatFile* vfat_file = &vfat->files[vfat->file_count];

    // Store long filename
    size_t fn_len = strlen(filename);
    if(fn_len >= sizeof(vfat_file->long_name)) fn_len = sizeof(vfat_file->long_name) - 1;
    memcpy(vfat_file->long_name, filename, fn_len);
    vfat_file->long_name[fn_len] = '\0';

    // Parse filename to 8.3 format
    memset(vfat_file->name, ' ', 11);

    const char* dot = strrchr(filename, '.'); // Use last dot
    size_t name_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if(name_len > 8) name_len = 8;

    for(size_t i = 0; i < name_len; i++) {
        vfat_file->name[i] = toupper(filename[i]);
    }

    if(dot && strlen(dot + 1) > 0) {
        size_t ext_len = strlen(dot + 1);
        if(ext_len > 3) ext_len = 3;
        for(size_t i = 0; i < ext_len; i++) {
            vfat_file->name[8 + i] = toupper(dot[1 + i]);
        }
    }

    vfat_file->sd_path = furi_string_alloc_set(sd_path);
    vfat_file->size = (uint32_t)file_size;
    vfat_file->source_type = FILE_SOURCE_SD_CARD;
    vfat_file->is_directory = false;
    vfat_file->parent_index = parent_index;
    vfat_file->start_cluster = vfat->next_cluster;

    uint32_t clusters_needed = (vfat_file->size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) /
                               (SECTORS_PER_CLUSTER * SECTOR_SIZE);
    vfat->next_cluster += clusters_needed;

    vfat->file_count++;

    FURI_LOG_I(
        TAG,
        "Added file to subdir: %.11s, parent: %d, size: %lu, cluster: %lu",
        vfat_file->name,
        parent_index,
        vfat_file->size,
        vfat_file->start_cluster);

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
    uint32_t cluster_count = total_sectors / SECTORS_PER_CLUSTER;
    uint32_t fat_size = ((cluster_count * 4) + SECTOR_SIZE - 1) / SECTOR_SIZE;

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
    memcpy(&buffer[71], "Boot2Flippr", 11);   // Volume label (11 bytes)
    memcpy(&buffer[82], "FAT32   ", 8);       // File system type (8 bytes)

    // FAT32 Bootstrap code - loads BOOT.CFG via iPXE embedded script
    // NOTE: For actual BIOS boot, you need to:
    // 1. Build iPXE with embedded script pointing to BOOT.CFG
    // 2. Replace this boot sector with the iPXE boot sector
    // 3. Keep the BPB (bytes 0-89) from this sector
    //
    // For now, this is a placeholder that prints a message
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

static void generate_fsinfo_sector(uint8_t* buffer) {
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

    // Free cluster count (0xFFFFFFFF = unknown)
    buffer[488] = 0xFF;
    buffer[489] = 0xFF;
    buffer[490] = 0xFF;
    buffer[491] = 0xFF;

    // Next free cluster (0xFFFFFFFF = unknown)
    buffer[492] = 0xFF;
    buffer[493] = 0xFF;
    buffer[494] = 0xFF;
    buffer[495] = 0xFF;

    // Trail signature
    buffer[508] = 0x00;
    buffer[509] = 0x00;
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
}

static void generate_fat_sector(VirtualFat* vfat, uint32_t fat_sector, uint8_t* buffer) {
    memset(buffer, 0, SECTOR_SIZE);

    uint32_t* fat = (uint32_t*)buffer;
    uint32_t entries_per_sector = SECTOR_SIZE / 4;
    uint32_t first_entry = fat_sector * entries_per_sector;

    // First FAT sector has special entries
    if(fat_sector == 0) {
        fat[0] = 0x0FFFFFF8; // Media descriptor
        fat[1] = 0x0FFFFFFF; // End of chain marker
        fat[2] = 0x0FFFFFFF; // Root directory (cluster 2) - end of chain
    }

    // Build FAT chain for each file and directory
    for(uint8_t i = 0; i < vfat->file_count; i++) {
        VirtualFatFile* file = &vfat->files[i];

        uint32_t clusters;
        if(file->is_directory) {
            // Directories always have at least 1 cluster
            clusters = 1;
        } else {
            // Files: calculate based on size
            clusters = (file->size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) /
                       (SECTORS_PER_CLUSTER * SECTOR_SIZE);
        }

        for(uint32_t c = 0; c < clusters; c++) {
            uint32_t cluster_num = file->start_cluster + c;

            if(cluster_num >= first_entry && cluster_num < first_entry + entries_per_sector) {
                uint32_t idx = cluster_num - first_entry;

                if(c == clusters - 1) {
                    fat[idx] = 0x0FFFFFFF; // End of chain
                } else {
                    fat[idx] = cluster_num + 1; // Next cluster
                }
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

    FURI_LOG_I(TAG, "Generating root directory, file_count: %u", vfat->file_count);

    // Only show files/dirs with parent_index == -1 (root)
    for(uint8_t i = 0; i < vfat->file_count && entry_count < (SECTOR_SIZE / 32); i++) {
        VirtualFatFile* file = &vfat->files[i];

        if(file->parent_index != -1) {
            FURI_LOG_D(
                TAG, "Skipping file %u (not in root, parent_index=%d)", i, file->parent_index);
            continue; // Skip non-root entries
        }

        // Write LFN entries if long name exists
        if(file->long_name[0] != '\0') {
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
        uint8_t attributes = file->is_directory ? 0x10 : 0x20;
        write_directory_entry(entry, file->name, attributes, file->start_cluster, file->size);

        FURI_LOG_I(
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

    FURI_LOG_I(TAG, "Root directory complete, total entries: %u", entry_count);
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
        if(file->long_name[0] != '\0') {
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
        uint8_t attributes = file->is_directory ? 0x10 : 0x20;
        write_directory_entry(entry, file->name, attributes, file->start_cluster, file->size);

        entry += 32;
        entry_count++;
    }
}

bool virtual_fat_read_sector(Storage* storage, VirtualFat* vfat, uint32_t lba, uint8_t* buffer) {
    if(vfat == NULL || buffer == NULL) return false;

    // Log which sectors are being read
    static uint32_t last_logged_lba = 0xFFFFFFFF;
    if(lba != last_logged_lba && lba < 100) { // Only log first 100 sectors to avoid spam
        FURI_LOG_I(TAG, "Reading LBA %lu", lba);
        last_logged_lba = lba;
    }

    // Partition size depends on scheme (use macros from virtual_fat.h)
    const uint32_t PARTITION_SECTORS = (vfat->partition_scheme == PARTITION_SCHEME_GPT_ONLY) ?
                                           PARTITION_SECTORS_GPT :
                                           PARTITION_SECTORS_MBR;

    uint32_t cluster_count = PARTITION_SECTORS / SECTORS_PER_CLUSTER;
    uint32_t fat_size = ((cluster_count * 4) + SECTOR_SIZE - 1) / SECTOR_SIZE;
    uint32_t fat1_start = PARTITION_START + RESERVED_SECTORS;
    uint32_t fat2_start = fat1_start + fat_size;
    uint32_t data_start = fat2_start + fat_size;

    // LBA 0: MBR or Protective MBR depending on partition scheme
    if(lba == 0) {
        if(vfat->partition_scheme == PARTITION_SCHEME_MBR_ONLY) {
            // MBR only - bootable FAT32 partition
            generate_mbr(buffer, PARTITION_START, PARTITION_SECTORS, 0xEF);
            FURI_LOG_D(TAG, "Generated MBR (MBR-only mode)");
        } else {
            // GPT only - protective MBR
            generate_protective_mbr(buffer, TOTAL_SECTORS);
            FURI_LOG_D(TAG, "Generated Protective MBR (GPT mode)");
        }
        return true;
    }

    // LBA 1: GPT Header (only in GPT mode)
    if(lba == 1) {
        if(vfat->partition_scheme == PARTITION_SCHEME_GPT_ONLY) {
            generate_gpt_header(buffer, TOTAL_SECTORS, PARTITION_START, PARTITION_SECTORS);
            FURI_LOG_D(TAG, "Generated GPT header at LBA 1");
        } else {
            // MBR mode: no GPT header
            memset(buffer, 0, SECTOR_SIZE);
        }
        return true;
    }

    // LBA 2: GPT Partition Entry Array (only in GPT mode)
    if(lba == 2) {
        if(vfat->partition_scheme == PARTITION_SCHEME_GPT_ONLY) {
            generate_gpt_partitions(buffer, PARTITION_START, PARTITION_SECTORS);
            FURI_LOG_D(TAG, "Generated GPT partitions at LBA 2");
        } else {
            // MBR mode: no GPT partitions
            memset(buffer, 0, SECTOR_SIZE);
        }
        return true;
    }

    // Empty sectors between GPT and partition (LBA 3 to PARTITION_START-1)
    if(lba > 2 && lba < PARTITION_START) {
        memset(buffer, 0, SECTOR_SIZE);
        return true;
    }

    // Backup GPT structures (only in GPT mode)
    if(vfat->partition_scheme == PARTITION_SCHEME_GPT_ONLY) {
        // Backup GPT partition array: starts at GPT_BACKUP_ARRAY_START
        if(lba >= GPT_BACKUP_ARRAY_START && lba < GPT_BACKUP_HEADER) {
            // Only first sector of backup partition array has data (like primary)
            if(lba == GPT_BACKUP_ARRAY_START) {
                generate_gpt_backup_partitions(buffer, PARTITION_START, PARTITION_SECTORS);
                FURI_LOG_D(TAG, "Generated backup GPT partitions at LBA %lu", lba);
            } else {
                memset(buffer, 0, SECTOR_SIZE);
            }
            return true;
        }

        // Backup GPT header: GPT_BACKUP_HEADER
        if(lba == GPT_BACKUP_HEADER) {
            generate_gpt_backup_header(buffer, TOTAL_SECTORS, PARTITION_START, PARTITION_SECTORS);
            FURI_LOG_D(TAG, "Generated backup GPT header at LBA %lu", lba);
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
        generate_fsinfo_sector(buffer);
        return true;
    }

    // Backup boot sector at LBA 7 (6 sectors after partition start)
    if(lba == PARTITION_START + 6) {
        generate_boot_sector(buffer, PARTITION_SECTORS);
        return true;
    }

    // Backup FS Info sector at LBA 8
    if(lba == PARTITION_START + 7) {
        generate_fsinfo_sector(buffer);
        return true;
    }

    // Other reserved sectors (empty)
    if(lba < fat1_start) {
        memset(buffer, 0, SECTOR_SIZE);
        return true;
    }

    // FAT1
    if(lba >= fat1_start && lba < fat2_start) {
        generate_fat_sector(vfat, lba - fat1_start, buffer);
        return true;
    }

    // FAT2 (copy of FAT1)
    if(lba >= fat2_start && lba < data_start) {
        generate_fat_sector(vfat, lba - fat2_start, buffer);
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
                uint32_t file_clusters = (file->size + (SECTORS_PER_CLUSTER * SECTOR_SIZE) - 1) /
                                         (SECTORS_PER_CLUSTER * SECTOR_SIZE);

                if(cluster_num >= file->start_cluster &&
                   cluster_num < file->start_cluster + file_clusters) {
                    // This sector belongs to this file
                    uint32_t file_cluster = cluster_num - file->start_cluster;
                    uint32_t file_sector = file_cluster * SECTORS_PER_CLUSTER + sector_in_cluster;
                    uint32_t offset = file_sector * SECTOR_SIZE;

                    FURI_LOG_I(
                        TAG,
                        "Reading file %.11s at cluster %lu, offset %lu, size %lu",
                        file->name,
                        cluster_num,
                        offset,
                        file->size);

                    // Trigger callback if set (only on first sector of file to avoid spam)
                    if(vfat->read_callback && offset == 0) {
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

                        if(file->source_type == FILE_SOURCE_MEMORY) {
                            // Read from RAM
                            FURI_LOG_D(
                                TAG,
                                "Reading %lu bytes from memory at offset %lu",
                                copy_size,
                                offset);
                            memcpy(buffer, file->memory_data + offset, copy_size);
                        } else if(file->source_type == FILE_SOURCE_SD_CARD) {
                            // Stream from SD card
                            File* sd_file = storage_file_alloc(storage);

                            if(storage_file_open(
                                   sd_file,
                                   furi_string_get_cstr(file->sd_path),
                                   FSAM_READ,
                                   FSOM_OPEN_EXISTING)) {
                                storage_file_seek(sd_file, offset, true);
                                uint16_t bytes_read =
                                    storage_file_read(sd_file, buffer, copy_size);
                                if(bytes_read != copy_size) {
                                    FURI_LOG_W(
                                        TAG,
                                        "SD read mismatch: expected %lu, got %u",
                                        copy_size,
                                        bytes_read);
                                }
                                storage_file_close(sd_file);
                            } else {
                                FURI_LOG_E(
                                    TAG,
                                    "Failed to open SD file: %s",
                                    furi_string_get_cstr(file->sd_path));
                            }

                            storage_file_free(sd_file);
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

uint32_t virtual_fat_get_total_sectors(VirtualFat* vfat) {
    UNUSED(vfat);
    return TOTAL_SECTORS;
}

void virtual_fat_set_partition_scheme(VirtualFat* vfat, PartitionScheme scheme) {
    if(vfat == NULL) return;
    vfat->partition_scheme = scheme;
    FURI_LOG_I(
        TAG, "Partition scheme set to: %s", scheme == PARTITION_SCHEME_MBR_ONLY ? "MBR" : "GPT");
}

void virtual_fat_set_read_callback(
    VirtualFat* vfat,
    VirtualFatFileReadCallback callback,
    void* context) {
    if(vfat == NULL) return;
    vfat->read_callback = callback;
    vfat->callback_context = context;
}
