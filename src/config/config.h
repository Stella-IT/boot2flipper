#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include "../disk/virtual_fat.h"

#define CONFIG_DIR_PATH          EXT_PATH("apps_data/boot2flipper")
#define CONFIG_FILE_PATH         EXT_PATH("apps_data/boot2flipper/config.b2f")
#define CONFIG_FILE_EXTENSION    ".b2f"
// Holds the path of the profile used last
#define CONFIG_LAST_PROFILE_PATH EXT_PATH("apps_data/boot2flipper/last_profile.txt")

/**
 * What iPXE runs once the network is up
 */
typedef enum {
    CHAINLOAD_TARGET_URL, // chain <chainload_url> <chainload_args>
    CHAINLOAD_TARGET_FILE, // A script from the SD card (chainload_file)
    CHAINLOAD_TARGET_SHELL, // The iPXE shell
    CHAINLOAD_TARGET_FLIPPER_SHELL, // iPXE commands typed on the Flipper
    CHAINLOAD_TARGET_COUNT,
} ChainloadTarget;

// network_interface values besides an interface name
#define NETWORK_INTERFACE_AUTO "auto" // DHCP on every interface, static IP on net0
#define NETWORK_INTERFACE_ASK  "ask" // Ask on the Flipper while booting

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
    FuriString* chainload_args; // Arguments for the chainloaded URL
    FuriString* chainload_file; // SD card path of the script for CHAINLOAD_TARGET_FILE
    ChainloadTarget chainload_target;
    FuriString* network_interface; // "auto", "ask" or an interface name (e.g., "net0")
    PartitionScheme partition_scheme; // MBR-only, GPT-only, or Hybrid
    bool status_report; // Show each boot step on the Flipper through .B2F/status
    bool b2f_quiet; // Run the .B2F writer with imgexec -q
    bool collect_sysinfo; // Send the host's SMBIOS and platform to .B2F/sysinfo
    bool write_log; // Keep a log file of the boot on the SD card
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

/**
 * Load the profile used last, or CONFIG_FILE_PATH
 * @param config Configuration to populate, left as is if no profile loads
 * @param file_path Set to the profile's path (CONFIG_FILE_PATH if none loads)
 * @return true if a profile was loaded
 */
bool config_load_last(Storage* storage, Boot2FlipperConfig* config, FuriString* file_path);

/**
 * Remember the profile used last
 */
void config_set_last(Storage* storage, const char* file_path);

/**
 * Get a profile's name: its file name without the extension
 */
void config_get_profile_name(const char* file_path, FuriString* name);
