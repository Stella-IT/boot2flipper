#pragma once

#include <stdint.h>

/**
 * SCSI Command Opcodes
 */
#define SCSI_CMD_TEST_UNIT_READY              0x00
#define SCSI_CMD_REQUEST_SENSE                0x03
#define SCSI_CMD_INQUIRY                      0x12
#define SCSI_CMD_MODE_SENSE_6                 0x1A
#define SCSI_CMD_START_STOP_UNIT              0x1B
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E
#define SCSI_CMD_READ_FORMAT_CAPACITY         0x23
#define SCSI_CMD_READ_CAPACITY_10             0x25
#define SCSI_CMD_READ_10                      0x28
#define SCSI_CMD_WRITE_10                     0x2A
#define SCSI_CMD_MODE_SENSE_10                0x5A

/**
 * SCSI Sense Keys
 */
#define SCSI_SENSE_NO_SENSE        0x00
#define SCSI_SENSE_RECOVERED_ERROR 0x01
#define SCSI_SENSE_NOT_READY       0x02
#define SCSI_SENSE_MEDIUM_ERROR    0x03
#define SCSI_SENSE_HARDWARE_ERROR  0x04
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05
#define SCSI_SENSE_UNIT_ATTENTION  0x06
#define SCSI_SENSE_DATA_PROTECT    0x07
#define SCSI_SENSE_BLANK_CHECK     0x08
#define SCSI_SENSE_ABORTED_COMMAND 0x0B
#define SCSI_SENSE_VOLUME_OVERFLOW 0x0D
#define SCSI_SENSE_MISCOMPARE      0x0E

/**
 * SCSI Additional Sense Codes
 */
#define SCSI_ASC_INVALID_COMMAND      0x20
#define SCSI_ASC_LBA_OUT_OF_RANGE     0x21
#define SCSI_ASC_INVALID_FIELD_IN_CDB 0x24
#define SCSI_ASC_MEDIUM_NOT_PRESENT   0x3A

/**
 * SCSI Constants
 */
#define SCSI_BLOCK_SIZE        512
#define SCSI_INQUIRY_DATA_SIZE 36
#define SCSI_SENSE_DATA_SIZE   18

/**
 * SCSI Device Type
 */
#define SCSI_DEVICE_TYPE_DIRECT_ACCESS 0x00

/**
 * Command Block structures
 */
typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint32_t lba;
    uint8_t group;
    uint16_t length;
    uint8_t control;
} __attribute__((packed)) SCSIRead10Cmd;

typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint32_t lba;
    uint8_t group;
    uint16_t length;
    uint8_t control;
} __attribute__((packed)) SCSIWrite10Cmd;

typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint16_t allocation_length;
    uint8_t control;
} __attribute__((packed)) SCSIInquiryCmd;
