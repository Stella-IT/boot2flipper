#include "ipxe_validator.h"

#define TAG "IpxeValidator"

bool ipxe_validate_binaries(Storage* storage, IpxeValidationResult* result) {
    FURI_LOG_I(TAG, "ipxe_validate_binaries started");

    if(result == NULL) {
        FURI_LOG_E(TAG, "result is NULL");
        return false;
    }

    FURI_LOG_I(TAG, "Clearing result struct");
    memset(result, 0, sizeof(IpxeValidationResult));

    // Check BIOS binary
    FURI_LOG_I(TAG, "Checking BIOS binary at: %s", IPXE_BIOS_PATH);
    result->bios_exists = storage_file_exists(storage, IPXE_BIOS_PATH);
    FURI_LOG_I(TAG, "BIOS exists: %d", result->bios_exists);

    if(result->bios_exists) {
        FURI_LOG_I(TAG, "Allocating file for BIOS size check");
        File* file = storage_file_alloc(storage);
        FURI_LOG_I(TAG, "Opening BIOS file");
        if(storage_file_open(file, IPXE_BIOS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_I(TAG, "Getting BIOS file size");
            result->bios_size = storage_file_size(file);
            FURI_LOG_I(TAG, "BIOS size: %lu", result->bios_size);
            storage_file_close(file);
        }
        storage_file_free(file);
    }

    // Check UEFI binary
    FURI_LOG_I(TAG, "Checking UEFI binary at: %s", IPXE_UEFI_PATH);
    result->uefi_exists = storage_file_exists(storage, IPXE_UEFI_PATH);
    FURI_LOG_I(TAG, "UEFI exists: %d", result->uefi_exists);

    if(result->uefi_exists) {
        FURI_LOG_I(TAG, "Allocating file for UEFI size check");
        File* file = storage_file_alloc(storage);
        FURI_LOG_I(TAG, "Opening UEFI file");
        if(storage_file_open(file, IPXE_UEFI_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_I(TAG, "Getting UEFI file size");
            result->uefi_size = storage_file_size(file);
            FURI_LOG_I(TAG, "UEFI size: %lu", result->uefi_size);
            storage_file_close(file);
        }
        storage_file_free(file);
    }

    // Check boot sector (optional)
    FURI_LOG_I(TAG, "Checking boot sector at: %s", IPXE_BOOT_SECTOR_PATH);
    result->boot_sector_exists = storage_file_exists(storage, IPXE_BOOT_SECTOR_PATH);
    FURI_LOG_I(TAG, "Boot sector exists: %d", result->boot_sector_exists);

    if(result->boot_sector_exists) {
        FURI_LOG_I(TAG, "Allocating file for boot sector size check");
        File* file = storage_file_alloc(storage);
        FURI_LOG_I(TAG, "Opening boot sector file");
        if(storage_file_open(file, IPXE_BOOT_SECTOR_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            FURI_LOG_I(TAG, "Getting boot sector file size");
            result->boot_sector_size = storage_file_size(file);
            FURI_LOG_I(TAG, "Boot sector size: %lu", result->boot_sector_size);
            storage_file_close(file);
        }
        storage_file_free(file);
    }

    FURI_LOG_I(
        TAG,
        "Validation: BIOS=%d (%lu bytes), UEFI=%d (%lu bytes), BootSector=%d (%lu bytes)",
        result->bios_exists,
        result->bios_size,
        result->uefi_exists,
        result->uefi_size,
        result->boot_sector_exists,
        result->boot_sector_size);

    bool validation_result = result->bios_exists && result->uefi_exists;
    FURI_LOG_I(TAG, "ipxe_validate_binaries returning: %d", validation_result);
    return validation_result;
}

bool ipxe_ensure_directory(Storage* storage) {
    // Create /ext/apps_data/boot2flipper if not exists
    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, "/ext/apps_data/boot2flipper");

    // Create ipxe subdirectory
    bool result = storage_simply_mkdir(storage, "/ext/apps_data/boot2flipper/ipxe");

    FURI_LOG_I(TAG, "iPXE directory ensured: %d", result);
    return result;
}

FuriString* ipxe_get_status_message(IpxeValidationResult* result) {
    FuriString* msg = furi_string_alloc();

    if(result->bios_exists && result->uefi_exists) {
        furi_string_printf(
            msg,
            "iPXE Ready\nBIOS: %lu KB\nUEFI: %lu KB",
            result->bios_size / 1024,
            result->uefi_size / 1024);
    } else if(!result->bios_exists && !result->uefi_exists) {
        furi_string_set_str(
            msg, "iPXE Not Found\nPlace files in:\n/ext/apps_data/\nboot2flipper/ipxe/");
    } else if(!result->bios_exists) {
        furi_string_set_str(msg, "Missing BIOS\nipxe.lkrn required");
    } else {
        furi_string_set_str(msg, "Missing UEFI\nipxe.efi required");
    }

    if(result->boot_sector_exists) {
        furi_string_cat_printf(msg, "\nBoot: %lu B", result->boot_sector_size);
    }

    return msg;
}
