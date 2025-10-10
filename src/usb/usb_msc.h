#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "usb_scsi.h"

/**
 * USB Mass Storage Class (MSC) Bulk-Only Transport
 * Implements USB MSC BOT protocol for mass storage emulation
 */

#define USB_MSC_EP_IN   0x82
#define USB_MSC_EP_OUT  0x02
#define USB_MSC_EP_SIZE 64

// CBW (Command Block Wrapper) signature
#define USB_MSC_CBW_SIGNATURE 0x43425355 // "USBC"
#define USB_MSC_CSW_SIGNATURE 0x53425355 // "USBS"

// CBW Flags
#define USB_MSC_CBW_FLAG_IN  0x80
#define USB_MSC_CBW_FLAG_OUT 0x00

// CSW Status
#define USB_MSC_CSW_STATUS_PASSED      0x00
#define USB_MSC_CSW_STATUS_FAILED      0x01
#define USB_MSC_CSW_STATUS_PHASE_ERROR 0x02

#pragma pack(push, 1)

// Command Block Wrapper (31 bytes)
typedef struct {
    uint32_t dSignature; // CBW signature
    uint32_t dTag; // Command Block Tag
    uint32_t dDataLength; // Transfer length
    uint8_t bmFlags; // Direction flag
    uint8_t bLUN; // Logical Unit Number
    uint8_t bCBLength; // Command Block length (1-16)
    uint8_t CB[16]; // Command Block (SCSI command)
} UsbMscCbw;

// Command Status Wrapper (13 bytes)
typedef struct {
    uint32_t dSignature; // CSW signature
    uint32_t dTag; // Command Block Tag (echoed from CBW)
    uint32_t dDataResidue; // Difference between expected and actual data
    uint8_t bStatus; // Command status
} UsbMscCsw;

#pragma pack(pop)

typedef struct UsbMscContext UsbMscContext;

/**
 * Allocate USB MSC context
 * @return Context pointer or NULL on error
 */
UsbMscContext* usb_msc_alloc(void);

/**
 * Free USB MSC context
 * @param ctx Context to free
 */
void usb_msc_free(UsbMscContext* ctx);

/**
 * Set SCSI context for MSC operations
 * @param ctx MSC context
 * @param scsi SCSI context (ownership NOT transferred)
 * @return true on success
 */
bool usb_msc_set_scsi(UsbMscContext* ctx, UsbScsiContext* scsi);

/**
 * Start USB MSC interface
 * @param ctx MSC context
 * @return true on success
 */
bool usb_msc_start(UsbMscContext* ctx);

/**
 * Stop USB MSC interface
 * @param ctx MSC context
 */
void usb_msc_stop(UsbMscContext* ctx);

/**
 * Check if USB MSC is active
 * @param ctx MSC context
 * @return true if active
 */
bool usb_msc_is_active(UsbMscContext* ctx);
