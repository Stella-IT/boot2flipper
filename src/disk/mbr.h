#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Generate MBR at LBA 0
// Returns true if successful
bool generate_mbr(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors,
    uint8_t partition_type // 0xEF for EFI, 0x0C for FAT32 LBA
);

// Generate a hybrid MBR for a GPT disk: an active FAT32 entry mirroring the GPT
// partition, plus a 0xEE entry covering the GPT structures before it
bool generate_hybrid_mbr(uint8_t* buffer, uint32_t partition_start_lba, uint32_t partition_sectors);
