#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Generate GPT header at LBA 1
bool generate_gpt_header(
    uint8_t* buffer,
    uint32_t total_sectors,
    uint32_t partition_start_lba,
    uint32_t partition_sectors);

// Generate GPT partition entry array at LBA 2
bool generate_gpt_partitions(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors);

// Generate backup GPT header at last LBA
bool generate_gpt_backup_header(
    uint8_t* buffer,
    uint32_t total_sectors,
    uint32_t partition_start_lba,
    uint32_t partition_sectors);

// Generate backup GPT partition entries (before last LBA)
bool generate_gpt_backup_partitions(
    uint8_t* buffer,
    uint32_t partition_start_lba,
    uint32_t partition_sectors);

// Generate protective MBR at LBA 0 for GPT
bool generate_protective_mbr(uint8_t* buffer, uint32_t total_sectors);
