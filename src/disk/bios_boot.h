#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Legacy BIOS boot loader (see tools/bios_boot/boot.S)
 *
 * The MBR boot code loads stage 2 from the gap before the FAT32 partition.
 * Stage 2 loads the kernel (ipxe.lkrn) and passes the command line and initrd.
 */

// Longest command line stage 2 passes (ipxe.lkrn's cmdline_size)
#define BIOS_BOOT_CMDLINE_MAX 2047

typedef struct {
    uint32_t kernel_lba; // First disk sector of the kernel file
    uint32_t kernel_size; // Kernel size in bytes (0 = not available)
    uint32_t initrd_lba; // First disk sector of the initrd file
    uint32_t initrd_size; // Initrd size in bytes (0 = none)
    uint32_t cmdline_lba; // First disk sector of the command line file
    uint32_t cmdline_size; // Command line size in bytes (0 = none), up to BIOS_BOOT_CMDLINE_MAX
} BiosBootParams;

// Write the boot code (bytes 0-439) of LBA 0, keeping the partition table
void bios_boot_write_mbr_code(uint8_t* buffer);

// Check whether LBA belongs to the stage 2 loader
bool bios_boot_is_stage2_sector(uint32_t lba);

// Generate a stage 2 loader sector with the parameter block filled in
void bios_boot_read_stage2_sector(uint32_t lba, const BiosBootParams* params, uint8_t* buffer);
