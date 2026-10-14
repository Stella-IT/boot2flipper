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
#define DATA_START_LBA      0x2000 // Root directory (cluster 2), at a round LBA
#define BPB_VOLUME_LABEL    "Boot2Flippr" // Boot sector label (BS_VolLab), 11 characters
#define VOLUME_LABEL        "BOOT2FLP   " // Root directory label entry (DIR_Name), shown by hosts
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

// FAT32 geometry for a partition size (compile-time constants for both schemes)
#define FAT_SIZE_SECTORS(partition_sectors) \
    (((partition_sectors) / SECTORS_PER_CLUSTER * 4 + SECTOR_SIZE - 1) / SECTOR_SIZE)
#define DATA_CLUSTERS(partition_sectors)                  \
    (((partition_sectors) - RESERVED_SECTORS -            \
      FAT_COPIES * FAT_SIZE_SECTORS(partition_sectors)) / \
     SECTORS_PER_CLUSTER)

// The reserved sectors fill the gap so the data area starts at DATA_START_LBA.
// Both schemes have the same FAT size, so clusters sit at the same LBA in every scheme.
#define RESERVED_SECTORS \
    (DATA_START_LBA - PARTITION_START - FAT_COPIES * FAT_SIZE_SECTORS(PARTITION_SECTORS_GPT))
#define CLUSTER_TO_LBA(cluster) (DATA_START_LBA + ((cluster) - 2) * SECTORS_PER_CLUSTER)
#define LBA_TO_CLUSTER(lba)     (((lba) - DATA_START_LBA) / SECTORS_PER_CLUSTER + 2)

// Directory entry attributes
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20

/**
 * Partition table scheme
 */
typedef enum {
    PARTITION_SCHEME_MBR_ONLY, // MBR (BIOS boot)
    PARTITION_SCHEME_GPT_ONLY, // GPT (UEFI boot)
    PARTITION_SCHEME_HYBRID, // GPT with a hybrid MBR (BIOS and UEFI boot)
    PARTITION_SCHEME_COUNT,
} PartitionScheme;

/**
 * How host writes are handled. Fixed files take writes in every mode.
 */
typedef enum {
    WRITE_MODE_READ_ONLY, // Write protected, other writes fail
    WRITE_MODE_MEMORY, // Other writes are kept in RAM, up to WRITE_CACHE_SECTORS
    WRITE_MODE_BAD, // Free space is marked bad so hosts can't add files, other writes dropped
    WRITE_MODE_COUNT,
} WriteMode;

// Sectors kept by WRITE_MODE_MEMORY, allocated when the mode is set. Writes past it fail.
#ifndef WRITE_CACHE_SECTORS
#define WRITE_CACHE_SECTORS 16
#endif

typedef struct VirtualFat VirtualFat;

/**
 * Callback function type for file read events
 * @param filename The file being read (long name if available, otherwise 8.3)
 * @param context User context pointer
 */
typedef void (*VirtualFatFileReadCallback)(const char* filename, void* context);

/**
 * Callback function type for host writes to a fixed file
 * Called from the USB thread after the data was copied into the file's buffer
 * @param context User context pointer
 * @param offset Offset in the file of the written data
 * @param size Number of bytes written
 */
typedef void (*VirtualFatFileWriteCallback)(void* context, uint32_t offset, uint32_t size);

/**
 * File source type
 */
typedef enum {
    FILE_SOURCE_MEMORY, // Data stored in RAM
    FILE_SOURCE_SD_CARD, // Data streamed from SD card file
    FILE_SOURCE_BUFFER, // Caller-owned buffer the host can write to
} FileSourceType;

/**
 * File entry in virtual filesystem
 */
typedef struct {
    char name[11]; // 8.3 filename (padded with spaces)
    char long_name[64]; // VFAT long filename (null-terminated ASCII), longer names are cut
    uint8_t short_case; // The 8.3 name holds the whole name: its lowercase flags + 1, else 0
    uint32_t size; // File size in bytes
    uint32_t start_cluster; // Starting cluster number
    FileSourceType source_type; // Where data comes from
    union {
        const uint8_t* memory_data; // For FILE_SOURCE_MEMORY
        FuriString* sd_path; // For FILE_SOURCE_SD_CARD
        uint8_t* buffer; // For FILE_SOURCE_BUFFER
    };
    VirtualFatFileWriteCallback write_callback; // For FILE_SOURCE_BUFFER, or NULL
    void* write_context;
    bool is_directory; // If true, this is a directory entry
    bool fixed; // start_cluster was given by the caller, other files are placed around it
    bool static_data; // memory_data belongs to the caller, not freed
    uint8_t attributes; // Extra attributes, e.g. FAT_ATTR_HIDDEN
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
 * Add file with data the caller keeps, e.g. a binary built into the app
 * The data is not copied, so it must stay valid while the filesystem is used.
 * @param vfat Instance
 * @param parent_dir Parent directory path (e.g., "EFI/BOOT"), or NULL for root
 * @param filename File name
 * @param data File data
 * @param size File size
 * @return true on success
 */
bool virtual_fat_add_static_file(
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    const uint8_t* data,
    uint32_t size);

/**
 * Add directory path to virtual filesystem, creating missing parents
 * @param vfat Instance
 * @param path Directory path (e.g., "EFI/BOOT")
 * @param attributes Extra attributes of the last directory (e.g., FAT_ATTR_HIDDEN)
 * @return true on success
 */
bool virtual_fat_add_directory_path(VirtualFat* vfat, const char* path, uint8_t attributes);

/**
 * Place files and directories added from now on at or after a cluster
 * Clusters before it stay free, e.g. for fixed files in a reserved area.
 * @param vfat Instance
 * @param cluster First cluster to use
 */
void virtual_fat_set_allocation_start(VirtualFat* vfat, uint32_t cluster);

/**
 * Add directory at a fixed cluster, creating missing parents
 * Other files are placed around it, so add fixed directories before them.
 * @param vfat Instance
 * @param path Directory path (e.g., "EFI/BOOT")
 * @param start_cluster Cluster of the directory, see LBA_TO_CLUSTER
 * @param attributes Extra attributes (e.g., FAT_ATTR_HIDDEN)
 * @return true on success, false if the cluster is taken or the directory exists
 */
bool virtual_fat_add_fixed_directory(
    VirtualFat* vfat,
    const char* path,
    uint32_t start_cluster,
    uint8_t attributes);

/**
 * Add file at a fixed cluster, so the host can find it without reading the filesystem
 * Reads return the buffer. With a callback, host writes go into the buffer and call
 * it; without one the file is read-only (writes are handled by the write mode).
 * Other files are placed around it, so add fixed files before them.
 * @param vfat Instance
 * @param parent_dir Parent directory path (e.g., "EFI/BOOT"), or NULL for root
 * @param filename File name
 * @param start_cluster First cluster of the file, see CLUSTER_TO_LBA
 * @param buffer File content, size bytes (ownership NOT transferred)
 * @param size File size
 * @param callback Called after host writes, or NULL for a read-only file
 * @param context User context pointer passed to callback
 * @return true on success, false if the clusters are taken
 */
bool virtual_fat_add_fixed_file(
    VirtualFat* vfat,
    const char* parent_dir,
    const char* filename,
    uint32_t start_cluster,
    uint8_t* buffer,
    uint32_t size,
    VirtualFatFileWriteCallback callback,
    void* context);

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
 * Write sector to virtual filesystem
 * Called by SCSI layer when host sends data. Writable fixed files store the data,
 * other sectors are handled by the write mode.
 * @param storage Storage instance (memory mode compares writes with file data)
 * @param vfat Instance
 * @param lba Logical Block Address (sector number)
 * @param buffer Input buffer (SECTOR_SIZE bytes)
 * @return true on success, false if the write mode refused it
 */
bool virtual_fat_write_sector(
    Storage* storage,
    VirtualFat* vfat,
    uint32_t lba,
    const uint8_t* buffer);

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
 * Set how host writes are handled
 * @param vfat Instance
 * @param mode Write mode
 */
void virtual_fat_set_write_mode(VirtualFat* vfat, WriteMode mode);

/**
 * Check if the disk should be reported as write protected
 * @param vfat Instance
 * @return true in WRITE_MODE_READ_ONLY
 */
bool virtual_fat_is_write_protected(VirtualFat* vfat);

/**
 * Set the files loaded by the legacy BIOS boot loader
 * @param vfat Instance
 * @param kernel_path File to boot (e.g., "IPXE.LKR")
 * @param initrd_path File passed as initrd, or NULL
 * @param cmdline_path File passed as the kernel command line (up to
 *        BIOS_BOOT_CMDLINE_MAX bytes), or NULL
 * @return true if all files were found
 */
bool virtual_fat_set_bios_boot_files(
    VirtualFat* vfat,
    const char* kernel_path,
    const char* initrd_path,
    const char* cmdline_path);

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
