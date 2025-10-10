#pragma once

#include <furi.h>
#include <storage/storage.h>

/**
 * Virtual FAT filesystem - generates FAT structures on-the-fly
 * No disk image file needed, everything generated in memory per SCSI read
 */

#define SECTOR_SIZE         512
#define SECTORS_PER_CLUSTER 1 // 1 sector/cluster for volumes ≤ 260MB (FAT32 spec)
#define TOTAL_SECTORS       262144 // 128MB disk (meets UEFI ESP minimum size)
#define RESERVED_SECTORS    32
#define FAT_COPIES          2

// Partition layout constants
#define PARTITION_START        2048 // 1MB alignment for macOS compatibility
#define GPT_BACKUP_SECTORS     33 // Backup GPT: 32 sectors array + 1 header
#define GPT_FIRST_USABLE       34 // First usable LBA (after primary GPT array)
#define GPT_LAST_USABLE        (TOTAL_SECTORS - GPT_BACKUP_SECTORS - 1) // Last usable LBA
#define GPT_BACKUP_ARRAY_START (TOTAL_SECTORS - GPT_BACKUP_SECTORS) // Backup partition array LBA
#define GPT_BACKUP_HEADER      (TOTAL_SECTORS - 1) // Backup GPT header LBA

// Partition sizes (mode-dependent)
#define PARTITION_SECTORS_MBR (TOTAL_SECTORS - PARTITION_START) // MBR: use all remaining sectors
#define PARTITION_SECTORS_GPT \
    (GPT_LAST_USABLE - PARTITION_START + 1) // GPT: reserve space for backup GPT

/**
 * Partition table scheme
 */
typedef enum {
    PARTITION_SCHEME_MBR_ONLY, // MBR (BIOS boot)
    PARTITION_SCHEME_GPT_ONLY, // GPT (UEFI boot)
} PartitionScheme;

typedef struct VirtualFat VirtualFat;

/**
 * Callback function type for file read events
 * @param filename The file being read (long name if available, otherwise 8.3)
 * @param context User context pointer
 */
typedef void (*VirtualFatFileReadCallback)(const char* filename, void* context);

/**
 * File source type
 */
typedef enum {
    FILE_SOURCE_MEMORY, // Data stored in RAM
    FILE_SOURCE_SD_CARD, // Data streamed from SD card file
} FileSourceType;

/**
 * File entry in virtual filesystem
 */
typedef struct {
    char name[11]; // 8.3 filename (padded with spaces)
    char long_name[256]; // VFAT long filename (null-terminated UTF-8)
    uint32_t size; // File size in bytes
    uint32_t start_cluster; // Starting cluster number
    FileSourceType source_type; // Where data comes from
    union {
        const uint8_t* memory_data; // For FILE_SOURCE_MEMORY
        FuriString* sd_path; // For FILE_SOURCE_SD_CARD
    };
    bool is_directory; // If true, this is a directory entry
    int8_t parent_index; // Index of parent directory (-1 for root)
} VirtualFatFile;

/**
 * Allocate virtual FAT filesystem
 * @return VirtualFat instance
 */
VirtualFat* virtual_fat_alloc(void);

/**
 * Free virtual FAT filesystem
 * @param vfat Instance
 */
void virtual_fat_free(VirtualFat* vfat);

/**
 * Add file to virtual filesystem
 * @param vfat Instance
 * @param filename 8.3 filename (e.g., "BOOT.CFG")
 * @param data File data (will be copied)
 * @param size File size
 * @return true on success
 */
bool virtual_fat_add_file(
    VirtualFat* vfat,
    const char* filename,
    const uint8_t* data,
    uint32_t size);

/**
 * Add text file to virtual filesystem
 * @param vfat Instance
 * @param filename 8.3 filename
 * @param text Text content (will be copied)
 * @return true on success
 */
bool virtual_fat_add_text_file(VirtualFat* vfat, const char* filename, const char* text);

/**
 * Add file from SD card to virtual filesystem
 * Data is streamed on-demand, not loaded into RAM
 * @param vfat Instance
 * @param filename 8.3 filename (e.g., "IPXE.LKR")
 * @param sd_path Path to file on SD card (e.g., "/ext/apps_data/boot2flipper/ipxe/ipxe.lkrn")
 * @return true on success
 */
bool virtual_fat_add_sd_file(
    Storage* storage,
    VirtualFat* vfat,
    const char* filename,
    const char* sd_path);

/**
 * Add directory to virtual filesystem
 * @param vfat Instance
 * @param dirname Directory name (8.3 format, e.g., "EFI")
 * @return true on success
 */
bool virtual_fat_add_directory(VirtualFat* vfat, const char* dirname);

/**
 * Add file to subdirectory in virtual filesystem
 * @param vfat Instance
 * @param parent_dir Parent directory name (e.g., "EFI/BOOT")
 * @param filename 8.3 filename (e.g., "BOOTX64.EFI")
 * @param sd_path Path to file on SD card
 * @return true on success
 */
bool virtual_fat_add_file_to_subdir(
    Storage* storage,
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    const char* sd_path);

/**
 * Read sector from virtual filesystem
 * Called by SCSI layer when host requests data
 * @param vfat Instance
 * @param lba Logical Block Address (sector number)
 * @param buffer Output buffer (must be SECTOR_SIZE bytes)
 * @return true on success
 */
bool virtual_fat_read_sector(Storage* storage, VirtualFat* vfat, uint32_t lba, uint8_t* buffer);

/**
 * Get total sector count
 * @param vfat Instance
 * @return Total sectors
 */
uint32_t virtual_fat_get_total_sectors(VirtualFat* vfat);

/**
 * Set partition scheme
 * @param vfat Instance
 * @param scheme Partition scheme to use
 */
void virtual_fat_set_partition_scheme(VirtualFat* vfat, PartitionScheme scheme);

/**
 * Set callback for file read events
 * @param vfat Instance
 * @param callback Callback function (or NULL to disable)
 * @param context User context pointer passed to callback
 */
void virtual_fat_set_read_callback(
    VirtualFat* vfat,
    VirtualFatFileReadCallback callback,
    void* context);
