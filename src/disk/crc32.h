#pragma once

#include <stdint.h>
#include <stddef.h>

// Calculate CRC32 checksum for GPT
uint32_t crc32_calculate(const uint8_t* data, size_t length);
