#include "usb_scsi.h"
#include <string.h>

#define TAG "UsbScsi"

typedef enum {
    SCSI_STATE_IDLE,
    SCSI_STATE_TX_DATA,
    SCSI_STATE_RX_DATA,
} ScsiState;

struct UsbScsiContext {
    Storage* storage;
    VirtualFat* vfat;
    bool active;

    // Command state
    ScsiState state;
    uint8_t sense_key;
    uint8_t asc; // Additional Sense Code

    // Data transmission mode
    bool is_small_data_mode; // true for INQUIRY/MODE_SENSE, false for READ_10

    // READ_10 / WRITE_10 state
    uint32_t current_lba;
    uint32_t remaining_blocks;
    uint8_t block_buffer[SCSI_BLOCK_SIZE];
    size_t buffer_offset;
};

UsbScsiContext* usb_scsi_alloc(void) {
    UsbScsiContext* ctx = malloc(sizeof(UsbScsiContext));
    memset(ctx, 0, sizeof(UsbScsiContext));

    ctx->vfat = NULL;
    ctx->active = false;
    ctx->state = SCSI_STATE_IDLE;
    ctx->sense_key = SCSI_SENSE_NO_SENSE;
    ctx->asc = 0;

    return ctx;
}

void usb_scsi_free(UsbScsiContext* ctx) {
    if(ctx == NULL) return;
    free(ctx);
}

bool usb_scsi_set_virtual_fat(UsbScsiContext* ctx, VirtualFat* vfat) {
    if(ctx == NULL || vfat == NULL) {
        FURI_LOG_E(TAG, "Invalid parameters");
        return false;
    }

    ctx->vfat = vfat;
    ctx->active = true;

    FURI_LOG_I(TAG, "Virtual FAT set, total sectors: %lu", virtual_fat_get_total_sectors(vfat));
    return true;
}

void usb_scsi_clear(UsbScsiContext* ctx) {
    if(ctx == NULL) return;

    ctx->vfat = NULL;
    ctx->active = false;
    ctx->state = SCSI_STATE_IDLE;

    FURI_LOG_I(TAG, "Virtual FAT cleared");
}

static void scsi_set_sense(UsbScsiContext* ctx, uint8_t sense_key, uint8_t asc) {
    ctx->sense_key = sense_key;
    ctx->asc = asc;
}

static bool scsi_cmd_test_unit_ready(UsbScsiContext* ctx) {
    UNUSED(ctx);
    FURI_LOG_D(TAG, "SCSI: TEST_UNIT_READY");
    return true;
}

static bool scsi_cmd_inquiry(UsbScsiContext* ctx, uint8_t* cmd) {
    FURI_LOG_D(TAG, "SCSI: INQUIRY");

    // Check for VPD (Vital Product Data)
    bool evpd = (cmd[1] & 0x01) != 0;
    uint8_t page_code = cmd[2];

    if(evpd) {
        // VPD page requested
        if(page_code == 0x00) {
            // Supported VPD Pages
            FURI_LOG_D(TAG, "INQUIRY: VPD Supported Pages (0x00)");
            uint8_t vpd_data[6] = {
                SCSI_DEVICE_TYPE_DIRECT_ACCESS, // Peripheral Device Type
                0x00, // Page Code
                0x00, // Reserved
                0x02, // Page Length (2 bytes following)
                0x00, // Supported page: 0x00 (this page)
                0x80 // Supported page: 0x80 (unit serial number)
            };
            memcpy(ctx->block_buffer, vpd_data, 6);
            ctx->is_small_data_mode = true;
            ctx->buffer_offset = 0;
            ctx->remaining_blocks = 6;
            ctx->state = SCSI_STATE_TX_DATA;
            return true;
        } else if(page_code == 0x80) {
            // Unit Serial Number
            FURI_LOG_D(TAG, "INQUIRY: VPD Unit Serial Number (0x80)");
            uint8_t vpd_data[8] = {
                SCSI_DEVICE_TYPE_DIRECT_ACCESS, // Peripheral Device Type
                0x80, // Page Code
                0x00, // Reserved
                0x04, // Page Length (4 bytes following)
                'F',
                'L',
                'P',
                '0' // Serial number: FLP0
            };
            memcpy(ctx->block_buffer, vpd_data, 8);
            ctx->is_small_data_mode = true;
            ctx->buffer_offset = 0;
            ctx->remaining_blocks = 8;
            ctx->state = SCSI_STATE_TX_DATA;
            return true;
        } else {
            // Unsupported VPD page
            FURI_LOG_W(TAG, "INQUIRY: Unsupported VPD page 0x%02X", page_code);
            scsi_set_sense(ctx, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_FIELD_IN_CDB);
            return false;
        }
    }

    // Standard INQUIRY response
    FURI_LOG_D(TAG, "INQUIRY: Standard");
    uint8_t inquiry_data[SCSI_INQUIRY_DATA_SIZE] = {
        SCSI_DEVICE_TYPE_DIRECT_ACCESS, // Peripheral Device Type
        0x80, // Removable
        0x00, // Version
        0x02, // Response Data Format
        0x1F, // Additional Length
        0x00, // Reserved
        0x00, // Reserved
        0x00, // Reserved
        // Vendor ID (8 bytes)
        'F',
        'L',
        'I',
        'P',
        'P',
        'E',
        'R',
        ' ',
        // Product ID (16 bytes)
        'B',
        'o',
        'o',
        't',
        '2',
        'F',
        'l',
        'i',
        'p',
        'p',
        'e',
        'r',
        ' ',
        ' ',
        ' ',
        ' ',
        // Product Revision (4 bytes)
        '1',
        '.',
        '0',
        ' '};

    memcpy(ctx->block_buffer, inquiry_data, SCSI_INQUIRY_DATA_SIZE);
    ctx->is_small_data_mode = true; // Byte-based transmission
    ctx->buffer_offset = 0;
    ctx->remaining_blocks = SCSI_INQUIRY_DATA_SIZE;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

static bool scsi_cmd_read_capacity_10(UsbScsiContext* ctx) {
    FURI_LOG_D(TAG, "SCSI: READ_CAPACITY_10");

    if(!ctx->vfat) {
        scsi_set_sense(ctx, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
        return false;
    }

    uint32_t total_blocks = virtual_fat_get_total_sectors(ctx->vfat);
    uint32_t last_lba = total_blocks - 1;

    // Prepare response (8 bytes)
    ctx->block_buffer[0] = (last_lba >> 24) & 0xFF;
    ctx->block_buffer[1] = (last_lba >> 16) & 0xFF;
    ctx->block_buffer[2] = (last_lba >> 8) & 0xFF;
    ctx->block_buffer[3] = last_lba & 0xFF;

    ctx->block_buffer[4] = (SCSI_BLOCK_SIZE >> 24) & 0xFF;
    ctx->block_buffer[5] = (SCSI_BLOCK_SIZE >> 16) & 0xFF;
    ctx->block_buffer[6] = (SCSI_BLOCK_SIZE >> 8) & 0xFF;
    ctx->block_buffer[7] = SCSI_BLOCK_SIZE & 0xFF;

    ctx->is_small_data_mode = true; // Byte-based transmission
    ctx->buffer_offset = 0;
    ctx->remaining_blocks = 8;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

static bool scsi_cmd_read_10(UsbScsiContext* ctx, uint8_t* cmd) {
    if(!ctx->vfat) {
        scsi_set_sense(ctx, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
        return false;
    }

    // Parse LBA and length from command
    uint32_t lba = ((uint32_t)cmd[2] << 24) | ((uint32_t)cmd[3] << 16) | ((uint32_t)cmd[4] << 8) |
                   cmd[5];
    uint16_t length = ((uint16_t)cmd[7] << 8) | cmd[8];

    FURI_LOG_D(TAG, "SCSI: READ_10 LBA=%lu, Length=%u", lba, length);

    // Check bounds
    uint32_t total_blocks = virtual_fat_get_total_sectors(ctx->vfat);
    if(lba + length > total_blocks) {
        FURI_LOG_E(TAG, "READ_10: LBA out of range");
        scsi_set_sense(ctx, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LBA_OUT_OF_RANGE);
        return false;
    }

    ctx->is_small_data_mode = false; // Sector-based transmission
    ctx->current_lba = lba;
    ctx->remaining_blocks = length;
    ctx->buffer_offset = 0;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

static bool scsi_cmd_mode_sense_6(UsbScsiContext* ctx) {
    FURI_LOG_D(TAG, "SCSI: MODE_SENSE_6");

    // Return minimal mode sense data
    uint8_t mode_sense_data[4] = {
        0x03, // Mode data length
        0x00, // Medium type
        0x80, // Device specific parameter (bit 7 = write protected)
        0x00 // Block descriptor length
    };

    memcpy(ctx->block_buffer, mode_sense_data, 4);
    ctx->is_small_data_mode = true; // Byte-based transmission
    ctx->buffer_offset = 0;
    ctx->remaining_blocks = 4;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

static bool scsi_cmd_mode_sense_10(UsbScsiContext* ctx) {
    FURI_LOG_D(TAG, "SCSI: MODE_SENSE_10");

    // Return minimal mode sense data (10-byte version)
    uint8_t mode_sense_data[8] = {
        0x00,
        0x06, // Mode data length (big-endian) - 6 additional bytes
        0x00, // Medium type
        0x80, // Device specific parameter (bit 7 = write protected)
        0x00,
        0x00, // Reserved
        0x00,
        0x00 // Block descriptor length (no block descriptors)
    };

    memcpy(ctx->block_buffer, mode_sense_data, 8);
    ctx->is_small_data_mode = true; // Byte-based transmission
    ctx->buffer_offset = 0;
    ctx->remaining_blocks = 8;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

static bool scsi_cmd_read_format_capacities(UsbScsiContext* ctx) {
    FURI_LOG_D(TAG, "SCSI: READ_FORMAT_CAPACITIES");

    if(!ctx->vfat) {
        scsi_set_sense(ctx, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
        return false;
    }

    uint32_t total_blocks = virtual_fat_get_total_sectors(ctx->vfat);
    uint32_t last_lba = total_blocks - 1;

    // Format: Capacity List Header (4 bytes) + Current/Maximum Capacity Descriptor (8 bytes)
    uint8_t format_data[12] = {
        0x00,
        0x00,
        0x00,
        0x08, // Capacity List Header: Reserved (3) + Capacity List Length (1)

        // Current/Maximum Capacity Descriptor
        (last_lba >> 24) & 0xFF, // Number of blocks (max LBA)
        (last_lba >> 16) & 0xFF,
        (last_lba >> 8) & 0xFF,
        last_lba & 0xFF,
        0x02, // Descriptor Code: 0x02 = Formatted Media
        (SCSI_BLOCK_SIZE >> 16) & 0xFF, // Block Length
        (SCSI_BLOCK_SIZE >> 8) & 0xFF,
        SCSI_BLOCK_SIZE & 0xFF};

    memcpy(ctx->block_buffer, format_data, 12);
    ctx->is_small_data_mode = true; // Byte-based transmission
    ctx->buffer_offset = 0;
    ctx->remaining_blocks = 12;
    ctx->state = SCSI_STATE_TX_DATA;

    return true;
}

bool usb_scsi_process_command(UsbScsiContext* ctx, uint8_t* cmd, uint8_t cmd_len) {
    if(ctx == NULL || cmd == NULL || cmd_len == 0) {
        return false;
    }

    // Reset state
    ctx->state = SCSI_STATE_IDLE;
    ctx->sense_key = SCSI_SENSE_NO_SENSE;
    ctx->asc = 0;

    uint8_t opcode = cmd[0];

    switch(opcode) {
    case SCSI_CMD_TEST_UNIT_READY:
        return scsi_cmd_test_unit_ready(ctx);

    case SCSI_CMD_INQUIRY:
        return scsi_cmd_inquiry(ctx, cmd);

    case SCSI_CMD_READ_FORMAT_CAPACITY:
        return scsi_cmd_read_format_capacities(ctx);

    case SCSI_CMD_READ_CAPACITY_10:
        return scsi_cmd_read_capacity_10(ctx);

    case SCSI_CMD_READ_10:
        return scsi_cmd_read_10(ctx, cmd);

    case SCSI_CMD_MODE_SENSE_6:
        return scsi_cmd_mode_sense_6(ctx);

    case SCSI_CMD_MODE_SENSE_10:
        return scsi_cmd_mode_sense_10(ctx);

    case SCSI_CMD_REQUEST_SENSE:
        FURI_LOG_D(TAG, "SCSI: REQUEST_SENSE");
        // Prepare sense data response (18 bytes)
        usb_scsi_get_sense_data(ctx, ctx->block_buffer);
        ctx->is_small_data_mode = true; // Byte-based transmission
        ctx->buffer_offset = 0;
        ctx->remaining_blocks = SCSI_SENSE_DATA_SIZE;
        ctx->state = SCSI_STATE_TX_DATA;
        return true;

    case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
        FURI_LOG_D(TAG, "SCSI: PREVENT_ALLOW_MEDIUM_REMOVAL (ignored)");
        // We can't physically lock the medium, just acknowledge the command
        return true;

    case SCSI_CMD_START_STOP_UNIT: {
        if(cmd_len < 5) {
            scsi_set_sense(ctx, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_FIELD_IN_CDB);
            return false;
        }
        bool eject = (cmd[4] & 0x02) != 0;
        bool start = (cmd[4] & 0x01) != 0;
        FURI_LOG_D(TAG, "SCSI: START_STOP_UNIT eject=%d start=%d", eject, start);
        // For virtual FAT read-only media, we can't physically eject
        // Just acknowledge the command
        // TODO: Could notify the app to exit USB mode if eject is requested
        return true;
    }

    case SCSI_CMD_WRITE_10:
        // Read-only filesystem
        FURI_LOG_W(TAG, "SCSI: WRITE_10 not supported (read-only)");
        scsi_set_sense(ctx, SCSI_SENSE_DATA_PROTECT, 0x27); // Write protected
        return false;

    default:
        FURI_LOG_W(TAG, "SCSI: Unknown command 0x%02X", opcode);
        scsi_set_sense(ctx, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_COMMAND);
        return false;
    }
}

size_t usb_scsi_transmit_data(UsbScsiContext* ctx, uint8_t* buffer, size_t max_len) {
    if(ctx == NULL || buffer == NULL) {
        FURI_LOG_E(TAG, "TX: NULL params");
        return 0;
    }

    if(ctx->state != SCSI_STATE_TX_DATA) {
        FURI_LOG_D(TAG, "TX: not in TX_DATA state (state=%d)", ctx->state);
        return 0;
    }

    Storage* storage = ctx->storage;
    size_t bytes_to_send = 0;

    // FURI_LOG_D(TAG, "TX: remaining=%u, offset=%u, lba=%lu, mode=%s",
    //            (unsigned int)ctx->remaining_blocks, (unsigned int)ctx->buffer_offset,
    //            ctx->current_lba, ctx->is_small_data_mode ? "byte" : "sector");

    if(ctx->is_small_data_mode) {
        // Small data response (INQUIRY, MODE_SENSE, etc.)
        // remaining_blocks = total bytes to send
        // Data is already in block_buffer

        // Check if all data already sent
        if(ctx->buffer_offset >= ctx->remaining_blocks) {
            FURI_LOG_D(TAG, "TX: small data complete");
            ctx->state = SCSI_STATE_IDLE;
            return 0;
        }

        size_t available = ctx->remaining_blocks - ctx->buffer_offset;
        bytes_to_send = (available < max_len) ? available : max_len;

        memcpy(buffer, ctx->block_buffer + ctx->buffer_offset, bytes_to_send);
        ctx->buffer_offset += bytes_to_send;

        if(ctx->buffer_offset >= ctx->remaining_blocks) {
            // All data sent
            ctx->state = SCSI_STATE_IDLE;
            ctx->remaining_blocks = 0;
        }
    } else {
        // Sector-based response (READ_10)
        // remaining_blocks = number of 512-byte sectors

        // Check if all sectors sent and buffer drained
        if(ctx->remaining_blocks == 0 && ctx->buffer_offset == 0) {
            FURI_LOG_D(TAG, "TX: all sectors complete");
            ctx->state = SCSI_STATE_IDLE;
            return 0;
        }

        // Need to load next sector?
        if(ctx->buffer_offset == 0 && ctx->remaining_blocks > 0) {
            if(!virtual_fat_read_sector(storage, ctx->vfat, ctx->current_lba, ctx->block_buffer)) {
                FURI_LOG_E(TAG, "Failed to read sector %lu", ctx->current_lba);
                ctx->state = SCSI_STATE_IDLE;
                return 0;
            }
            ctx->current_lba++;
            ctx->remaining_blocks--;
        }

        // Send data from current sector
        size_t available = SCSI_BLOCK_SIZE - ctx->buffer_offset;
        bytes_to_send = (available < max_len) ? available : max_len;

        memcpy(buffer, ctx->block_buffer + ctx->buffer_offset, bytes_to_send);
        ctx->buffer_offset += bytes_to_send;

        if(ctx->buffer_offset >= SCSI_BLOCK_SIZE) {
            // Sector complete, reset offset for next sector
            ctx->buffer_offset = 0;

            if(ctx->remaining_blocks == 0) {
                // All sectors sent
                ctx->state = SCSI_STATE_IDLE;
            }
        }
    }

    return bytes_to_send;
}

bool usb_scsi_receive_data(UsbScsiContext* ctx, uint8_t* buffer, size_t len) {
    UNUSED(ctx);
    UNUSED(buffer);
    UNUSED(len);
    // Read-only filesystem, no WRITE support
    return false;
}

bool usb_scsi_has_tx_data(UsbScsiContext* ctx) {
    return ctx != NULL && ctx->state == SCSI_STATE_TX_DATA;
}

void usb_scsi_get_sense_data(UsbScsiContext* ctx, uint8_t* buffer) {
    if(ctx == NULL || buffer == NULL) return;

    memset(buffer, 0, SCSI_SENSE_DATA_SIZE);

    buffer[0] = 0x70; // Response code: Current errors
    buffer[2] = ctx->sense_key;
    buffer[7] = 0x0A; // Additional sense length
    buffer[12] = ctx->asc;
    buffer[13] = 0x00; // Additional Sense Code Qualifier

    // Clear sense after reporting
    ctx->sense_key = SCSI_SENSE_NO_SENSE;
    ctx->asc = 0;
}

void usb_scsi_set_storage(UsbScsiContext* ctx, Storage* storage) {
    if(ctx) {
        ctx->storage = storage;
    }
}
