#pragma once

#include "../disk/virtual_fat.h"

/**
 * .B2F directory: files the booted host reads and writes by LBA
 *
 * The directory and files sit at fixed LBAs in an area reserved right after the
 * root directory (0x2000), each sized to its file, and other files start after
 * the area. A program on the host (e.g. started from iPXE) can use them with raw
 * sector reads/writes, without parsing FAT. The directory is hidden and system,
 * like Windows' own hidden folders.
 *
 *   .B2F/info    B2F_INFO_LBA, read-only. Starts with B2F_MAGIC, so it can also
 *                be found by scanning the disk:
 *                  0  char[8]  B2F_MAGIC
 *                  8  u16      B2F_INFO_VERSION
 *                 10  u16      entry count
 *                 16  entries of 32 bytes: char[16] name (NUL padded),
 *                     u32 lba, u32 size in bytes, u32 flags (B2F_INFO_WRITABLE),
 *                     u32 reserved
 *                All numbers are little-endian.
 *   .B2F/status  B2F_STATUS_LBA, B2F_STATUS_SIZE bytes
 *                ASCII text shown on the Flipper, up to the first NUL.
 *   .B2F/frame   B2F_FRAME_LBA, B2F_FRAME_SIZE bytes
 *                128x64 1-bit frame shown on the Flipper while not all zero.
 *                Rows of 16 bytes from the top, least significant bit is the
 *                leftmost pixel (XBM, as canvas_draw_xbm takes), 1 is black.
 *   .B2F/bin/b2f.efi, .B2F/bin/b2f.pxe
 *                Write status/frame from iPXE on UEFI and BIOS, see tools/b2f_efi
 *                and tools/b2f_pxe. The BIOS loader passes b2f.pxe as initrd.
 */
#define B2F_DIR_PATH      ".B2F"
#define B2F_BIN_PATH      B2F_DIR_PATH "/bin"
#define B2F_EFI_PATH      B2F_BIN_PATH "/b2f.efi"
#define B2F_PXE_PATH      B2F_BIN_PATH "/b2f.pxe"
#define B2F_MAGIC         "B2F INFO"
#define B2F_INFO_VERSION  1
#define B2F_INFO_WRITABLE 0x01

// Fixed LBAs, part of the host interface (see README). 0x2001-0x20FF is
// reserved for .B2F, other files start at B2F_AREA_END_LBA.
#define B2F_DIR_LBA      0x2001
#define B2F_INFO_LBA     (B2F_DIR_LBA + 1)
#define B2F_STATUS_LBA   (B2F_INFO_LBA + 1)
#define B2F_STATUS_SIZE  SECTOR_SIZE
#define B2F_FRAME_LBA    (B2F_STATUS_LBA + B2F_STATUS_SIZE / SECTOR_SIZE)
#define B2F_FRAME_SIZE   (128 * 64 / 8)
#define B2F_AREA_END_LBA 0x2100

// Clusters are 1 sector, so every LBA in the data area is a cluster
_Static_assert(SECTORS_PER_CLUSTER == 1, ".B2F LBAs need 1 sector clusters");
_Static_assert(B2F_DIR_LBA == DATA_START_LBA + 1, ".B2F must follow the root directory");
_Static_assert(
    B2F_FRAME_LBA + B2F_FRAME_SIZE / SECTOR_SIZE <= B2F_AREA_END_LBA,
    ".B2F files don't fit the reserved area");

typedef struct B2fDir B2fDir;

/**
 * Callback function type for .B2F changes, called from the USB thread
 * @param context User context pointer
 */
typedef void (*B2fDirUpdateCallback)(void* context);

B2fDir* b2f_dir_alloc(void);

void b2f_dir_free(B2fDir* b2f);

/**
 * Add the .B2F directory and files to a virtual FAT
 * Call before adding other files, they are then placed after the reserved area.
 * @return true on success
 */
bool b2f_dir_add_to(B2fDir* b2f, VirtualFat* vfat);

void b2f_dir_set_update_callback(B2fDir* b2f, B2fDirUpdateCallback callback, void* context);

/**
 * Copy the status text, printable ASCII and newlines only
 * @param text Output buffer
 * @param size Output buffer size
 * @return Text length, 0 if the host didn't set a status
 */
size_t b2f_dir_get_status(B2fDir* b2f, char* text, size_t size);

/**
 * Lock the frame against host writes while drawing it
 * @return Frame (B2F_FRAME_SIZE bytes), or NULL if the host didn't write one.
 *         Call b2f_dir_release_frame either way.
 */
const uint8_t* b2f_dir_acquire_frame(B2fDir* b2f);

void b2f_dir_release_frame(B2fDir* b2f);
