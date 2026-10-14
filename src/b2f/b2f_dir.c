#include "b2f_dir.h"
#include "b2f_efi_image.h"
#include "b2f_pxe_image.h"
#include "../log.h"

#define TAG "B2fDir"

typedef struct {
    B2fDir* b2f;
    uint8_t* host; // Buffer of the virtual FAT file, written by the USB thread
    uint8_t* shown; // Copy read by the GUI, under b2f->mutex
} B2fFile;

struct B2fDir {
    FuriMutex* mutex;
    B2fDirUpdateCallback callback;
    void* callback_context;

    uint8_t info[SECTOR_SIZE];
    uint8_t status_host[B2F_STATUS_SIZE];
    uint8_t status_shown[B2F_STATUS_SIZE];
    uint8_t frame_host[B2F_FRAME_SIZE];
    uint8_t frame_shown[B2F_FRAME_SIZE];
    uint8_t mode_host[B2F_MODE_SIZE];
    uint8_t mode_shown[B2F_MODE_SIZE];
    B2fMode mode;

    B2fFile status_file;
    B2fFile frame_file;
    B2fFile mode_file;
};

// Writers for iPXE, from .B2F/bin up to the end of the area
_Static_assert(
    (sizeof(b2f_efi_image) + SECTOR_SIZE - 1) / SECTOR_SIZE +
            (sizeof(b2f_pxe_image) + SECTOR_SIZE - 1) / SECTOR_SIZE <
        B2F_AREA_END_LBA - B2F_BIN_LBA,
    ".B2F/bin doesn't fit the reserved area");

static void put_u16(uint8_t* p, uint16_t value) {
    p[0] = value & 0xFF;
    p[1] = value >> 8;
}

static void put_u32(uint8_t* p, uint32_t value) {
    put_u16(p, value & 0xFFFF);
    put_u16(p + 2, value >> 16);
}

static void b2f_dir_add_info_entry(
    uint8_t* info,
    const char* name,
    uint32_t lba,
    uint32_t size,
    uint32_t flags) {
    uint16_t count = info[10] | info[11] << 8;
    uint8_t* entry = info + 16 + count * 32;
    strncpy((char*)entry, name, 16);
    put_u32(entry + 16, lba);
    put_u32(entry + 20, size);
    put_u32(entry + 24, flags);
    put_u16(info + 10, count + 1);
}

B2fDir* b2f_dir_alloc(void) {
    B2fDir* b2f = malloc(sizeof(B2fDir));
    memset(b2f, 0, sizeof(B2fDir));
    b2f->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    b2f->status_file = (B2fFile){b2f, b2f->status_host, b2f->status_shown};
    b2f->frame_file = (B2fFile){b2f, b2f->frame_host, b2f->frame_shown};
    b2f->mode_file = (B2fFile){b2f, b2f->mode_host, b2f->mode_shown};
    b2f->mode = B2fModeStatus;

    memcpy(b2f->info, B2F_MAGIC, 8);
    put_u16(b2f->info + 8, B2F_INFO_VERSION);
    b2f_dir_add_info_entry(
        b2f->info, "status", B2F_STATUS_LBA, B2F_STATUS_SIZE, B2F_INFO_WRITABLE);
    b2f_dir_add_info_entry(b2f->info, "frame", B2F_FRAME_LBA, B2F_FRAME_SIZE, B2F_INFO_WRITABLE);
    b2f_dir_add_info_entry(b2f->info, "mode", B2F_MODE_LBA, B2F_MODE_SIZE, B2F_INFO_WRITABLE);
    return b2f;
}

void b2f_dir_free(B2fDir* b2f) {
    if(b2f == NULL) return;
    furi_mutex_free(b2f->mutex);
    free(b2f);
}

// The word in .B2F/mode, up to the first NUL, space or newline
static B2fMode b2f_dir_parse_mode(const uint8_t* text) {
    size_t length = 0;
    while(length < B2F_MODE_SIZE && text[length] > ' ')
        length++;

    if(length == strlen(B2F_MODE_FRAMEBUFFER) && memcmp(text, B2F_MODE_FRAMEBUFFER, length) == 0) {
        return B2fModeFramebuffer;
    }
    return B2fModeStatus;
}

// The virtual FAT wrote to the host buffer, show it
static void b2f_dir_file_written(void* context, uint32_t offset, uint32_t size) {
    B2fFile* file = context;
    B2fDir* b2f = file->b2f;

    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    memcpy(file->shown + offset, file->host + offset, size);
    if(file == &b2f->mode_file) b2f->mode = b2f_dir_parse_mode(b2f->mode_shown);
    furi_mutex_release(b2f->mutex);

    if(b2f->callback) b2f->callback(b2f->callback_context);
}

bool b2f_dir_add_to(B2fDir* b2f, VirtualFat* vfat) {
    if(b2f == NULL || vfat == NULL) return false;

    if(!virtual_fat_add_fixed_directory(
           vfat, B2F_DIR_PATH, LBA_TO_CLUSTER(B2F_DIR_LBA), FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM) ||
       !virtual_fat_add_fixed_file(
           vfat,
           B2F_DIR_PATH,
           "info",
           LBA_TO_CLUSTER(B2F_INFO_LBA),
           b2f->info,
           SECTOR_SIZE,
           NULL,
           NULL) ||
       !virtual_fat_add_fixed_file(
           vfat,
           B2F_DIR_PATH,
           "status",
           LBA_TO_CLUSTER(B2F_STATUS_LBA),
           b2f->status_host,
           B2F_STATUS_SIZE,
           b2f_dir_file_written,
           &b2f->status_file) ||
       !virtual_fat_add_fixed_file(
           vfat,
           B2F_DIR_PATH,
           "frame",
           LBA_TO_CLUSTER(B2F_FRAME_LBA),
           b2f->frame_host,
           B2F_FRAME_SIZE,
           b2f_dir_file_written,
           &b2f->frame_file) ||
       !virtual_fat_add_fixed_file(
           vfat,
           B2F_DIR_PATH,
           "mode",
           LBA_TO_CLUSTER(B2F_MODE_LBA),
           b2f->mode_host,
           B2F_MODE_SIZE,
           b2f_dir_file_written,
           &b2f->mode_file)) {
        B2F_LOG_E(TAG, "Failed to add " B2F_DIR_PATH);
        return false;
    }

    // Writers for iPXE, from .B2F/bin, so more fixed files fit before it
    if(!virtual_fat_add_fixed_directory(vfat, B2F_BIN_PATH, LBA_TO_CLUSTER(B2F_BIN_LBA), 0)) {
        B2F_LOG_E(TAG, "Failed to add " B2F_BIN_PATH);
        return false;
    }
    virtual_fat_set_allocation_start(vfat, LBA_TO_CLUSTER(B2F_BIN_LBA) + 1);
    if(!virtual_fat_add_static_file(
           vfat, B2F_BIN_PATH, "b2f.efi", b2f_efi_image, sizeof(b2f_efi_image)) ||
       !virtual_fat_add_static_file(
           vfat, B2F_BIN_PATH, "b2f.pxe", b2f_pxe_image, sizeof(b2f_pxe_image))) {
        B2F_LOG_E(TAG, "Failed to add " B2F_BIN_PATH);
        return false;
    }

    // Other files go after the reserved area
    virtual_fat_set_allocation_start(vfat, LBA_TO_CLUSTER(B2F_AREA_END_LBA));
    return true;
}

void b2f_dir_set_update_callback(B2fDir* b2f, B2fDirUpdateCallback callback, void* context) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    b2f->callback = callback;
    b2f->callback_context = context;
    furi_mutex_release(b2f->mutex);
}

size_t b2f_dir_get_status(B2fDir* b2f, char* text, size_t size) {
    if(size == 0) return 0;
    size_t length = 0;

    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    for(size_t i = 0; i < B2F_STATUS_SIZE && length + 1 < size; i++) {
        char ch = (char)b2f->status_shown[i];
        if(ch == '\0') break;
        if(ch == '\n' || (ch >= ' ' && ch <= '~')) text[length++] = ch; // Drops '\r' etc.
    }
    furi_mutex_release(b2f->mutex);

    // Trailing newlines don't make a line
    while(length > 0 && text[length - 1] == '\n')
        length--;
    text[length] = '\0';
    return length;
}

B2fMode b2f_dir_get_mode(B2fDir* b2f) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    B2fMode mode = b2f->mode;
    furi_mutex_release(b2f->mutex);
    return mode;
}

const uint8_t* b2f_dir_acquire_frame(B2fDir* b2f) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    return b2f->frame_shown;
}

void b2f_dir_release_frame(B2fDir* b2f) {
    furi_mutex_release(b2f->mutex);
}
