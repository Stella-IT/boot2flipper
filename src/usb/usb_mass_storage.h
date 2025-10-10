#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>

/**
 * USB Mass Storage module for Boot2Flipper
 * Based on flipperzero-good-faps/mass_storage implementation
 */

typedef struct UsbMassStorageContext UsbMassStorageContext;

/**
 * Initialize USB Mass Storage context
 * @return Context pointer or NULL on error
 */
UsbMassStorageContext* usb_mass_storage_alloc(void);

/**
 * Free USB Mass Storage context
 * @param ctx Context to free
 */
void usb_mass_storage_free(UsbMassStorageContext* ctx);

/**
 * Start USB Mass Storage emulation
 * @param ctx Context
 * @param disk_path Path to disk image file on SD card
 * @return true on success, false on error
 */
bool usb_mass_storage_start(UsbMassStorageContext* ctx, const char* disk_path);

/**
 * Stop USB Mass Storage emulation
 * @param ctx Context
 */
void usb_mass_storage_stop(UsbMassStorageContext* ctx);

/**
 * Check if USB Mass Storage is active
 * @param ctx Context
 * @return true if active, false otherwise
 */
bool usb_mass_storage_is_active(UsbMassStorageContext* ctx);

/**
 * Get current status message
 * @param ctx Context
 * @return Status string (do not free)
 */
const char* usb_mass_storage_get_status(UsbMassStorageContext* ctx);
