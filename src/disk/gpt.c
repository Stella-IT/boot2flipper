#include "gpt.h"
#include "crc32.h"
#include "virtual_fat.h"
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512

// EFI System Partition Type GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
static const uint8_t ESP_TYPE_GUID[16] =
    {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

// Static partition GUID for our partition
static const uint8_t PART_GUID[16] =
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};

// Static disk GUID
static const uint8_t DISK_GUID[16] =
    {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

static void build_partition_entry(
    uint8_t* entry,
    uint32_t partition_start_lba,
    uint32_t partition_sectors) {
    // Partition Type GUID
    memcpy(&entry[0], ESP_TYPE_GUID, 16);

    // Unique Partition GUID
    memcpy(&entry[16], PART_GUID, 16);

    // First LBA (8 bytes)
    entry[32] = partition_start_lba & 0xFF;
    entry[33] = (partition_start_lba >> 8) & 0xFF;
    entry[34] = (partition_start_lba >> 16) & 0xFF;
    entry[35] = (partition_start_lba >> 24) & 0xFF;
    entry[36] = 0x00;
    entry[37] = 0x00;
    entry[38] = 0x00;
    entry[39] = 0x00;

    // Last LBA (8 bytes)
    uint32_t last_lba = partition_start_lba + partition_sectors - 1;
    entry[40] = last_lba & 0xFF;
    entry[41] = (last_lba >> 8) & 0xFF;
    entry[42] = (last_lba >> 16) & 0xFF;
    entry[43] = (last_lba >> 24) & 0xFF;
    entry[44] = 0x00;
    entry[45] = 0x00;
    entry[46] = 0x00;
    entry[47] = 0x00;

    // Attribute flags (8 bytes) - bit 0 = required partition
    entry[48] = 0x01;
    memset(&entry[49], 0, 7);

    // Partition name "EFI System" in UTF-16LE
    const uint16_t name[] = {'E', 'F', 'I', ' ', 'S', 'y', 's', 't', 'e', 'm', 0};
    for(int i = 0; i < 11 && i < 36; i++) {
        entry[56 + i * 2] = name[i] & 0xFF;
        entry[56 + i * 2 + 1] = (name[i] >> 8) & 0xFF;
    }
}

bool generate_gpt_header(
    uint8_t* buffer,
    uint32_t total_sectors,
    uint32_t partition_start_lba,
    uint32_t partition_sectors) {
    memset(buffer, 0, SECTOR_SIZE);

    // Pre-calculate partition array CRC32
    // GPT spec: 128 entries × 128 bytes = 16384 bytes (32 sectors)
    // Use malloc to avoid stack overflow on embedded systems
    uint8_t* part_array = malloc(128 * 128);
    if(!part_array) {
        return false;
    }
    memset(part_array, 0, 128 * 128);
    build_partition_entry(part_array, partition_start_lba, partition_sectors);
    uint32_t part_array_crc = crc32_calculate(part_array, 128 * 128);
    free(part_array);

    // GPT Header signature
    memcpy(&buffer[0], "EFI PART", 8);

    // Version 1.0
    buffer[8] = 0x00;
    buffer[9] = 0x00;
    buffer[10] = 0x01;
    buffer[11] = 0x00;

    // Header size (92 bytes)
    buffer[12] = 0x5C;
    buffer[13] = 0x00;
    buffer[14] = 0x00;
    buffer[15] = 0x00;

    // CRC32 (will calculate later)
    memset(&buffer[16], 0, 4);

    // Reserved
    memset(&buffer[20], 0, 4);

    // Current LBA (this header at LBA 1)
    buffer[24] = 0x01;
    memset(&buffer[25], 0, 7);

    // Backup LBA (last sector)
    uint32_t backup_lba = total_sectors - 1;
    buffer[32] = backup_lba & 0xFF;
    buffer[33] = (backup_lba >> 8) & 0xFF;
    buffer[34] = (backup_lba >> 16) & 0xFF;
    buffer[35] = (backup_lba >> 24) & 0xFF;
    memset(&buffer[36], 0, 4);

    // First usable LBA for partitions (use macro)
    buffer[40] = GPT_FIRST_USABLE & 0xFF;
    buffer[41] = (GPT_FIRST_USABLE >> 8) & 0xFF;
    buffer[42] = (GPT_FIRST_USABLE >> 16) & 0xFF;
    buffer[43] = (GPT_FIRST_USABLE >> 24) & 0xFF;
    memset(&buffer[44], 0, 4);

    // Last usable LBA (use macro)
    buffer[48] = GPT_LAST_USABLE & 0xFF;
    buffer[49] = (GPT_LAST_USABLE >> 8) & 0xFF;
    buffer[50] = (GPT_LAST_USABLE >> 16) & 0xFF;
    buffer[51] = (GPT_LAST_USABLE >> 24) & 0xFF;
    memset(&buffer[52], 0, 4);

    // Disk GUID
    memcpy(&buffer[56], DISK_GUID, 16);

    // Partition entries starting LBA (LBA 2)
    buffer[72] = 0x02;
    memset(&buffer[73], 0, 7);

    // Number of partition entries (128)
    buffer[80] = 0x80;
    memset(&buffer[81], 0, 3);

    // Size of partition entry (128 bytes)
    buffer[84] = 0x80;
    memset(&buffer[85], 0, 3);

    // Partition array CRC32
    buffer[88] = part_array_crc & 0xFF;
    buffer[89] = (part_array_crc >> 8) & 0xFF;
    buffer[90] = (part_array_crc >> 16) & 0xFF;
    buffer[91] = (part_array_crc >> 24) & 0xFF;

    // Calculate header CRC32 (with CRC field = 0)
    uint32_t header_crc = crc32_calculate(buffer, 92);
    buffer[16] = header_crc & 0xFF;
    buffer[17] = (header_crc >> 8) & 0xFF;
    buffer[18] = (header_crc >> 16) & 0xFF;
    buffer[19] = (header_crc >> 24) & 0xFF;

    return true;
}

bool generate_gpt_partitions(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors) {
    memset(buffer, 0, SECTOR_SIZE);
    build_partition_entry(buffer, partition_start_lba, partition_sectors);
    return true;
}

bool generate_gpt_backup_header(
    uint8_t* buffer,
    uint32_t total_sectors,
    uint32_t partition_start_lba,
    uint32_t partition_sectors) {
    memset(buffer, 0, SECTOR_SIZE);

    // Pre-calculate partition array CRC32 (same as primary)
    uint8_t* part_array = malloc(128 * 128);
    if(!part_array) {
        return false;
    }
    memset(part_array, 0, 128 * 128);
    build_partition_entry(part_array, partition_start_lba, partition_sectors);
    uint32_t part_array_crc = crc32_calculate(part_array, 128 * 128);
    free(part_array);

    // GPT Header signature
    memcpy(&buffer[0], "EFI PART", 8);

    // Version 1.0
    buffer[8] = 0x00;
    buffer[9] = 0x00;
    buffer[10] = 0x01;
    buffer[11] = 0x00;

    // Header size (92 bytes)
    buffer[12] = 0x5C;
    buffer[13] = 0x00;
    buffer[14] = 0x00;
    buffer[15] = 0x00;

    // CRC32 (will calculate later)
    memset(&buffer[16], 0, 4);

    // Reserved
    memset(&buffer[20], 0, 4);

    // Current LBA (this backup header at last sector)
    uint32_t current_lba = total_sectors - 1;
    buffer[24] = current_lba & 0xFF;
    buffer[25] = (current_lba >> 8) & 0xFF;
    buffer[26] = (current_lba >> 16) & 0xFF;
    buffer[27] = (current_lba >> 24) & 0xFF;
    memset(&buffer[28], 0, 4);

    // Backup LBA (primary header at LBA 1)
    buffer[32] = 0x01;
    memset(&buffer[33], 0, 7);

    // First usable LBA for partitions (use macro, same as primary)
    buffer[40] = GPT_FIRST_USABLE & 0xFF;
    buffer[41] = (GPT_FIRST_USABLE >> 8) & 0xFF;
    buffer[42] = (GPT_FIRST_USABLE >> 16) & 0xFF;
    buffer[43] = (GPT_FIRST_USABLE >> 24) & 0xFF;
    memset(&buffer[44], 0, 4);

    // Last usable LBA (use macro, same as primary)
    buffer[48] = GPT_LAST_USABLE & 0xFF;
    buffer[49] = (GPT_LAST_USABLE >> 8) & 0xFF;
    buffer[50] = (GPT_LAST_USABLE >> 16) & 0xFF;
    buffer[51] = (GPT_LAST_USABLE >> 24) & 0xFF;
    memset(&buffer[52], 0, 4);

    // Disk GUID (same as primary)
    memcpy(&buffer[56], DISK_GUID, 16);

    // Partition entries starting LBA (use macro for backup array start)
    buffer[72] = GPT_BACKUP_ARRAY_START & 0xFF;
    buffer[73] = (GPT_BACKUP_ARRAY_START >> 8) & 0xFF;
    buffer[74] = (GPT_BACKUP_ARRAY_START >> 16) & 0xFF;
    buffer[75] = (GPT_BACKUP_ARRAY_START >> 24) & 0xFF;
    memset(&buffer[76], 0, 4);

    // Number of partition entries (128)
    buffer[80] = 0x80;
    memset(&buffer[81], 0, 3);

    // Size of partition entry (128 bytes)
    buffer[84] = 0x80;
    memset(&buffer[85], 0, 3);

    // Partition array CRC32 (same as primary)
    buffer[88] = part_array_crc & 0xFF;
    buffer[89] = (part_array_crc >> 8) & 0xFF;
    buffer[90] = (part_array_crc >> 16) & 0xFF;
    buffer[91] = (part_array_crc >> 24) & 0xFF;

    // Calculate header CRC32 (with CRC field = 0)
    uint32_t header_crc = crc32_calculate(buffer, 92);
    buffer[16] = header_crc & 0xFF;
    buffer[17] = (header_crc >> 8) & 0xFF;
    buffer[18] = (header_crc >> 16) & 0xFF;
    buffer[19] = (header_crc >> 24) & 0xFF;

    return true;
}

bool generate_gpt_backup_partitions(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors) {
    // Same as primary partitions
    return generate_gpt_partitions(buffer, partition_start_lba, partition_sectors);
}

bool generate_protective_mbr(uint8_t* buffer, uint32_t total_sectors) {
    memset(buffer, 0, SECTOR_SIZE);

    // Partition entry 1 (protective GPT partition)
    buffer[446] = 0x00; // Not bootable
    buffer[447] = 0x00; // Starting CHS (head)
    buffer[448] = 0x02; // Starting CHS (sector) - sector 2
    buffer[449] = 0x00; // Starting CHS (cylinder)
    buffer[450] = 0xEE; // Partition type: GPT protective
    buffer[451] = 0xFF; // Ending CHS (head)
    buffer[452] = 0xFF; // Ending CHS (sector)
    buffer[453] = 0xFF; // Ending CHS (cylinder)

    // Starting LBA = 1 (GPT header location)
    buffer[454] = 0x01;
    buffer[455] = 0x00;
    buffer[456] = 0x00;
    buffer[457] = 0x00;

    // Total sectors in partition (entire disk - 1)
    uint32_t protective_sectors = total_sectors - 1;
    buffer[458] = protective_sectors & 0xFF;
    buffer[459] = (protective_sectors >> 8) & 0xFF;
    buffer[460] = (protective_sectors >> 16) & 0xFF;
    buffer[461] = (protective_sectors >> 24) & 0xFF;

    // MBR signature
    buffer[510] = 0x55;
    buffer[511] = 0xAA;

    return true;
}
