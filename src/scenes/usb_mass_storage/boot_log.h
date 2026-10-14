#pragma once

#include <furi.h>
#include <storage/storage.h>

/**
 * Log file of a boot on the SD card, named by the time it starts
 * (BOOT_LOG_DIR/YYYY-MM-DD_HH-MM-SS.log). Lines start with the time.
 * Thread safe: the USB thread logs the host's writes, the GUI the user's answers.
 */
typedef struct BootLog BootLog;

#define BOOT_LOG_DIR EXT_PATH("apps_data/boot2flipper/logs")

/**
 * Create a new log file
 * @return Log, or NULL if the file can't be created
 */
BootLog* boot_log_open(Storage* storage);

/** Write a line, synced to the card so it survives unplugging */
void boot_log_write(BootLog* log, const char* format, ...)
    _ATTRIBUTE((__format__(__printf__, 2, 3)));

void boot_log_close(BootLog* log);
