#pragma once

#include <furi.h>
#include <storage/storage.h>

/**
 * iPXE binary paths on SD card
 */
#define IPXE_BIOS_PATH        "/ext/apps_data/boot2flipper/ipxe/ipxe.lkrn"
#define IPXE_UEFI_PATH        "/ext/apps_data/boot2flipper/ipxe/ipxe.efi"
#define IPXE_BOOT_SECTOR_PATH "/ext/apps_data/boot2flipper/ipxe/boot_sector.bin"

/**
 * Validation result
 */
typedef struct {
    bool bios_exists;
    bool uefi_exists;
    bool boot_sector_exists;
    uint32_t bios_size;
    uint32_t uefi_size;
    uint32_t boot_sector_size;
} IpxeValidationResult;

/**
 * Validate iPXE binaries on SD card
 * @param storage Storage instance
 * @param result Output validation result
 * @return true if both BIOS and UEFI binaries exist
 */
bool ipxe_validate_binaries(Storage* storage, IpxeValidationResult* result);

/**
 * Check if iPXE directory exists, create if not
 * @param storage Storage instance
 * @return true if directory exists or was created successfully
 */
bool ipxe_ensure_directory(Storage* storage);

/**
 * Get human-readable status message
 * @param result Validation result
 * @return Status message (caller must free)
 */
FuriString* ipxe_get_status_message(IpxeValidationResult* result);
