#include "mbr.h"
#include <string.h>

#define SECTOR_SIZE 512

static void write_partition_entry(
    uint8_t* partition,
    bool bootable,
    uint8_t partition_type,
    uint32_t start_lba,
    uint32_t sectors) {
    // Calculate CHS addressing
    uint32_t start_cylinder = start_lba / (255 * 63);
    uint32_t start_head = (start_lba / 63) % 255;
    uint32_t start_sector = (start_lba % 63) + 1;
    uint32_t end_lba = start_lba + sectors - 1;
    uint32_t end_cylinder = end_lba / (255 * 63);
    uint32_t end_head = (end_lba / 63) % 255;
    uint32_t end_sector = (end_lba % 63) + 1;

    partition[0] = bootable ? 0x80 : 0x00;
    partition[1] = start_head & 0xFF;
    partition[2] = (start_sector & 0x3F) | ((start_cylinder >> 2) & 0xC0);
    partition[3] = start_cylinder & 0xFF;
    partition[4] = partition_type; // Partition type
    partition[5] = end_head & 0xFF;
    partition[6] = (end_sector & 0x3F) | ((end_cylinder >> 2) & 0xC0);
    partition[7] = end_cylinder & 0xFF;

    // LBA addressing
    partition[8] = start_lba & 0xFF;
    partition[9] = (start_lba >> 8) & 0xFF;
    partition[10] = (start_lba >> 16) & 0xFF;
    partition[11] = (start_lba >> 24) & 0xFF;
    partition[12] = sectors & 0xFF;
    partition[13] = (sectors >> 8) & 0xFF;
    partition[14] = (sectors >> 16) & 0xFF;
    partition[15] = (sectors >> 24) & 0xFF;
}

bool generate_mbr(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors,
    uint8_t partition_type) {
    memset(buffer, 0, SECTOR_SIZE);

    // Boot code (bytes 0-439) is filled in by bios_boot_write_mbr_code()

    // Partition table starts at offset 446 (0x1BE)
    write_partition_entry(
        &buffer[446], true, partition_type, partition_start_lba, partition_sectors);

    // MBR signature
    buffer[510] = 0x55;
    buffer[511] = 0xAA;

    return true;
}

bool generate_hybrid_mbr(uint8_t* buffer, uint32_t partition_start_lba, uint32_t partition_sectors) {
    // Entry 1: the GPT's FAT32 partition, active, for BIOSes that need an active
    // partition and for firmware that reads the MBR instead of the GPT
    generate_mbr(buffer, partition_start_lba, partition_sectors, 0xEF);

    // Entry 2: protective 0xEE over the GPT structures before the partition, so
    // UEFI still finds the GPT. It must not overlap entry 1.
    write_partition_entry(&buffer[462], false, 0xEE, 1, partition_start_lba - 1);

    return true;
}
