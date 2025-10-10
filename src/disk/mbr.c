#include "mbr.h"
#include <string.h>

#define SECTOR_SIZE 512

bool generate_mbr(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors,
    uint8_t partition_type) {
    memset(buffer, 0, SECTOR_SIZE);

    /* clang-format off */
    // MBR Bootstrap code - chainloads partition boot sector
    const uint8_t mbr_bootstrap[] = {
        0xFA,                   // CLI (disable interrupts)
        0x33, 0xC0,             // XOR AX, AX
        0x8E, 0xD0,             // MOV SS, AX
        0xBC, 0x00, 0x7C,       // MOV SP, 0x7C00
        0x8E, 0xD8,             // MOV DS, AX
        0x8E, 0xC0,             // MOV ES, AX
        0xFB,                   // STI (enable interrupts)
        0xBE, 0xBE, 0x01,       // MOV SI, 0x01BE (partition table)
        0xB4, 0x02,             // MOV AH, 0x02 (read sectors)
        0xB0, 0x01,             // MOV AL, 0x01 (1 sector)
        0xBB, 0x00, 0x7C,       // MOV BX, 0x7C00 (load address)
        0xB9, 0x01, 0x00,       // MOV CX, 0x0001 (cylinder 0, sector 1)
        0xB6, 0x00,             // MOV DH, 0x00 (head 0)
        0x8A, 0x14,             // MOV DL, [SI] (drive number)
        0x8A, 0x74, 0x01,       // MOV DH, [SI+1] (start head)
        0x8A, 0x4C, 0x02,       // MOV CL, [SI+2] (start sector)
        0x8A, 0x6C, 0x03,       // MOV CH, [SI+3] (start cylinder)
        0x8A, 0x54, 0x08,       // MOV DL, [SI+8] (LBA start - use as drive)
        0xCD, 0x13,             // INT 0x13 (BIOS disk read)
        0x72, 0x0A,             // JC error
        0xEA, 0x00, 0x7C, 0x00, 0x00, // JMP 0x0000:0x7C00
        // Error handler
        0xB4, 0x0E,             // MOV AH, 0x0E (teletype)
        0xB0, 0x45,             // MOV AL, 'E'
        0xCD, 0x10,             // INT 0x10
        0xEB, 0xFE              // JMP $ (hang)
    };
    /* clang-format on */

    memcpy(buffer, mbr_bootstrap, sizeof(mbr_bootstrap));

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
