#pragma once

#include <furi.h>
#include "../disk/virtual_fat.h"
#include "usb_scsi_commands.h"

/**
 * USB SCSI command handler
 * Implements SCSI Block Commands for USB Mass Storage
 * Uses virtual FAT filesystem for on-the-fly sector generation
 */

typedef struct UsbScsiContext UsbScsiContext;

/**
 * Allocate SCSI context
 * @return Context pointer or NULL on error
 */
UsbScsiContext* usb_scsi_alloc(void);

/**
 * Free SCSI context
 * @param ctx Context to free
 */
void usb_scsi_free(UsbScsiContext* ctx);

/**
 * Set virtual FAT filesystem for SCSI operations
 * @param ctx Context
 * @param vfat Virtual FAT instance (ownership NOT transferred)
 * @return true on success, false on error
 */
bool usb_scsi_set_virtual_fat(UsbScsiContext* ctx, VirtualFat* vfat);

/**
 * Clear virtual FAT
 * @param ctx Context
 */
void usb_scsi_clear(UsbScsiContext* ctx);

/**
 * Process SCSI command
 * @param ctx Context
 * @param cmd Command buffer
 * @param cmd_len Command length
 * @return true if command accepted
 */
bool usb_scsi_process_command(UsbScsiContext* ctx, uint8_t* cmd, uint8_t cmd_len);

/**
 * Transmit data to host
 * @param ctx Context
 * @param buffer Output buffer
 * @param max_len Maximum bytes to write
 * @return Number of bytes written, or 0 if no data
 */
size_t usb_scsi_transmit_data(UsbScsiContext* ctx, uint8_t* buffer, size_t max_len);

/**
 * Receive data from host
 * @param ctx Context
 * @param buffer Input buffer
 * @param len Data length
 * @return true on success
 */
bool usb_scsi_receive_data(UsbScsiContext* ctx, uint8_t* buffer, size_t len);

/**
 * Check if command has data to transmit
 * @param ctx Context
 * @return true if data pending
 */
bool usb_scsi_has_tx_data(UsbScsiContext* ctx);

/**
 * Get sense data for REQUEST_SENSE command
 * @param ctx Context
 * @param buffer Output buffer (must be 18 bytes)
 */
void usb_scsi_get_sense_data(UsbScsiContext* ctx, uint8_t* buffer);

/**
 * Set storage for SCSI context
 * @param ctx Context
 * @param storage Storage instance
 */
void usb_scsi_set_storage(UsbScsiContext* ctx, Storage* storage);
