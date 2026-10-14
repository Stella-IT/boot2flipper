#include "boot_log.h"
#include <furi_hal_rtc.h>
#include <stdarg.h>
#include "../../log.h"

#define TAG "BootLog"

struct BootLog {
    FuriMutex* mutex;
    File* file;
    FuriString* line;
};

BootLog* boot_log_open(Storage* storage) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    FuriString* path = furi_string_alloc_printf(
        "%s/%04u-%02u-%02u_%02u-%02u-%02u.log",
        BOOT_LOG_DIR,
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

    storage_common_mkdir(storage, BOOT_LOG_DIR);
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        B2F_LOG_E(TAG, "Can't create %s", furi_string_get_cstr(path));
        storage_file_free(file);
        furi_string_free(path);
        return NULL;
    }
    furi_string_free(path);

    BootLog* log = malloc(sizeof(BootLog));
    log->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    log->file = file;
    log->line = furi_string_alloc();
    return log;
}

void boot_log_write(BootLog* log, const char* format, ...) {
    if(log == NULL) return;
    DateTime now;
    furi_hal_rtc_get_datetime(&now);

    furi_mutex_acquire(log->mutex, FuriWaitForever);
    furi_string_printf(log->line, "[%02u:%02u:%02u] ", now.hour, now.minute, now.second);
    va_list args;
    va_start(args, format);
    furi_string_cat_vprintf(log->line, format, args);
    va_end(args);
    furi_string_push_back(log->line, '\n');
    storage_file_write(log->file, furi_string_get_cstr(log->line), furi_string_size(log->line));
    storage_file_sync(log->file);
    furi_mutex_release(log->mutex);
}

void boot_log_close(BootLog* log) {
    if(log == NULL) return;
    storage_file_close(log->file);
    storage_file_free(log->file);
    furi_string_free(log->line);
    furi_mutex_free(log->mutex);
    free(log);
}
