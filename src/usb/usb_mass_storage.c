#include "usb_mass_storage.h"
#include "usb_scsi.h"
#include <furi_hal_usb.h>

#define TAG "UsbMassStorage"

struct UsbMassStorageContext {
    bool active;
    FuriString* status;
    FuriString* disk_path;
    UsbScsiContext* scsi_ctx;
};

UsbMassStorageContext* usb_mass_storage_alloc(void) {
    UsbMassStorageContext* ctx = malloc(sizeof(UsbMassStorageContext));

    ctx->active = false;
    ctx->status = furi_string_alloc_set_str("Idle");
    ctx->disk_path = furi_string_alloc();
    ctx->scsi_ctx = usb_scsi_alloc();

    return ctx;
}

void usb_mass_storage_free(UsbMassStorageContext* ctx) {
    if(ctx == NULL) return;

    if(ctx->active) {
        usb_mass_storage_stop(ctx);
    }

    usb_scsi_free(ctx->scsi_ctx);
    furi_string_free(ctx->status);
    furi_string_free(ctx->disk_path);

    free(ctx);
}

bool usb_mass_storage_start(UsbMassStorageContext* ctx, const char* disk_path) {
    if(ctx == NULL || ctx->active) {
        FURI_LOG_E(TAG, "Invalid context or already active");
        return false;
    }

    FURI_LOG_I(TAG, "Starting USB Mass Storage with disk: %s", disk_path);

    furi_string_set_str(ctx->disk_path, disk_path);

    // TODO: Initialize SCSI context with virtual FAT
    // For now, just a placeholder - virtual FAT will be set from scene
    UNUSED(disk_path);

    // TODO: Set USB mode to Mass Storage
    // furi_hal_usb_set_config(&usb_mass_storage, ctx);

    ctx->active = true;
    furi_string_set_str(ctx->status, "Active");

    FURI_LOG_I(TAG, "USB Mass Storage started successfully");
    return true;
}

void usb_mass_storage_stop(UsbMassStorageContext* ctx) {
    if(ctx == NULL || !ctx->active) {
        return;
    }

    FURI_LOG_I(TAG, "Stopping USB Mass Storage");

    // TODO: Restore USB mode
    // furi_hal_usb_set_config(&usb_cdc_single, NULL);

    usb_scsi_clear(ctx->scsi_ctx);

    ctx->active = false;
    furi_string_set_str(ctx->status, "Stopped");

    FURI_LOG_I(TAG, "USB Mass Storage stopped");
}

bool usb_mass_storage_is_active(UsbMassStorageContext* ctx) {
    return ctx != NULL && ctx->active;
}

const char* usb_mass_storage_get_status(UsbMassStorageContext* ctx) {
    if(ctx == NULL) return "Invalid context";
    return furi_string_get_cstr(ctx->status);
}
