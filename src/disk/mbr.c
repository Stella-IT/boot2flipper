#include "mbr.h"
#include <string.h>

#define SECTOR_SIZE 512

bool generate_mbr(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors,
    uint8_t partition_type) {
    memset(buffer, 0, SECTOR_SIZE);

    // Boot code (bytes 0-439) is filled in by bios_boot_write_mbr_code()

    // Partition table starts at offset 446 (0x1BE)
    uint8_t* partition = &buffer[446];

    // Calculate CHS addressing
    uint32_t start_cylinder = partition_start_lba / (255 * 63);
    uint32_t start_head = (partition_start_lba / 63) % 255;
    uint32_t start_sector = (partition_start_lba % 63) + 1;
    uint32_t end_lba = partition_start_lba + partition_sectors - 1;
    uint32_t end_cylinder = end_lba / (255 * 63);
    uint32_t end_head = (end_lba / 63) % 255;
    uint32_t end_sector = (end_lba % 63) + 1;

    // Partition entry
    partition[0] = 0x80; // Bootable
    partition[1] = start_head & 0xFF;
    partition[2] = (start_sector & 0x3F) | ((start_cylinder >> 2) & 0xC0);
    partition[3] = start_cylinder & 0xFF;
    partition[4] = partition_type; // Partition type
    partition[5] = end_head & 0xFF;
    partition[6] = (end_sector & 0x3F) | ((end_cylinder >> 2) & 0xC0);
    partition[7] = end_cylinder & 0xFF;

    // LBA addressing
    partition[8] = partition_start_lba & 0xFF;
    partition[9] = (partition_start_lba >> 8) & 0xFF;
    partition[10] = (partition_start_lba >> 16) & 0xFF;
    partition[11] = (partition_start_lba >> 24) & 0xFF;
    partition[12] = partition_sectors & 0xFF;
    partition[13] = (partition_sectors >> 8) & 0xFF;
    partition[14] = (partition_sectors >> 16) & 0xFF;
    partition[15] = (partition_sectors >> 24) & 0xFF;

    // MBR signature
    buffer[510] = 0x55;
    buffer[511] = 0xAA;

    return true;
}
