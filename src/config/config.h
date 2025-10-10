#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include "../disk/virtual_fat.h"

#define CONFIG_DIR_PATH       EXT_PATH("apps_data/boot2flipper")
#define CONFIG_FILE_PATH      EXT_PATH("apps_data/boot2flipper/config.b2f")
#define CONFIG_FILE_EXTENSION ".b2f"

/**
 * Configuration structure for boot2flipper
 */
typedef struct {
    bool dhcp;
    FuriString* ip_addr;
    FuriString* subnet_mask;
    FuriString* gateway;
    FuriString* dns;
    FuriString* chainload_url;
    FuriString* network_interface; // Network interface name (e.g., "net0", "net1")
    PartitionScheme partition_scheme; // MBR-only, GPT-only, or Hybrid
    bool chainload_enabled; // Enable/disable chainloading
} Boot2FlipperConfig;

/**
 * Initialize a new configuration with default values
 * @return Pointer to newly allocated config
 */
Boot2FlipperConfig* config_alloc();

/**
 * Free configuration memory
 * @param config Configuration to free
 */
void config_free(Boot2FlipperConfig* config);

/**
 * Save configuration to file
 * @param storage Storage instance
 * @param config Configuration to save
 * @param file_path Full path to the file (including extension)
 * @return true if successful, false otherwise
 */
bool config_save(Storage* storage, const Boot2FlipperConfig* config, const char* file_path);

/**
 * Load configuration from file
 * @param storage Storage instance
 * @param config Configuration structure to populate
 * @param file_path Full path to the file
 * @return true if successful, false otherwise
 */
bool config_load(Storage* storage, Boot2FlipperConfig* config, const char* file_path);

/**
 * Copy configuration from source to destination
 * @param dest Destination configuration
 * @param src Source configuration
 */
void config_copy(Boot2FlipperConfig* dest, const Boot2FlipperConfig* src);
