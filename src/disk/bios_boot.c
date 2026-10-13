#include "bios_boot.h"
#include "bios_boot_image.h"
#include <string.h>

#define SECTOR_SIZE   512
#define MBR_CODE_SIZE 440 // Followed by disk signature and partition table
#define STAGE2_OFFSET SECTOR_SIZE

static void put_le32(uint8_t* buffer, uint32_t value) {
    buffer[0] = value & 0xFF;
    buffer[1] = (value >> 8) & 0xFF;
    buffer[2] = (value >> 16) & 0xFF;
    buffer[3] = (value >> 24) & 0xFF;
}

void bios_boot_write_mbr_code(uint8_t* buffer) {
    memcpy(buffer, bios_boot_image, MBR_CODE_SIZE);
}

bool bios_boot_is_stage2_sector(uint32_t lba) {
    return lba >= BIOS_BOOT_STAGE2_LBA && lba < BIOS_BOOT_STAGE2_LBA + BIOS_BOOT_STAGE2_SECTORS;
}

void bios_boot_read_stage2_sector(uint32_t lba, const BiosBootParams* params, uint8_t* buffer) {
    uint32_t image_offset = STAGE2_OFFSET + (lba - BIOS_BOOT_STAGE2_LBA) * SECTOR_SIZE;
    memcpy(buffer, &bios_boot_image[image_offset], SECTOR_SIZE);

    // The parameter block lives in the first stage 2 sector
    if(image_offset != STAGE2_OFFSET) return;

    put_le32(&buffer[BIOS_BOOT_KERNEL_LBA_OFFSET - STAGE2_OFFSET], params->kernel_lba);
    put_le32(&buffer[BIOS_BOOT_KERNEL_SIZE_OFFSET - STAGE2_OFFSET], params->kernel_size);
    put_le32(&buffer[BIOS_BOOT_INITRD_LBA_OFFSET - STAGE2_OFFSET], params->initrd_lba);
    put_le32(&buffer[BIOS_BOOT_INITRD_SIZE_OFFSET - STAGE2_OFFSET], params->initrd_size);
}
