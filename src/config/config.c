#include "config.h"
#include <storage/storage.h>
#include <string.h>
#include "../log.h"

#define TAG "Boot2FlipperConfig"

Boot2FlipperConfig* config_alloc() {
    Boot2FlipperConfig* config = malloc(sizeof(Boot2FlipperConfig));

    config->dhcp = true;
    config->ip_addr = furi_string_alloc_set("192.168.1.10");
    config->subnet_mask = furi_string_alloc_set("255.255.255.0");
    config->gateway = furi_string_alloc_set("192.168.1.1");
    config->dns = furi_string_alloc_set("8.8.8.8");
    config->chainload_url = furi_string_alloc_set("http://boot.ipxe.org/demo/boot.php");
    config->chainload_args = furi_string_alloc();
    config->chainload_file = furi_string_alloc();
    config->chainload_target = CHAINLOAD_TARGET_URL;
    config->network_interface = furi_string_alloc_set(NETWORK_INTERFACE_AUTO);
    config->partition_scheme = PARTITION_SCHEME_HYBRID; // Default: BIOS and UEFI
    config->status_report = true;
    config->b2f_quiet = true;
    config->collect_sysinfo = false;
    config->write_log = false;

    return config;
}

void config_free(Boot2FlipperConfig* config) {
    if(config == NULL) return;

    furi_string_free(config->ip_addr);
    furi_string_free(config->subnet_mask);
    furi_string_free(config->gateway);
    furi_string_free(config->dns);
    furi_string_free(config->chainload_url);
    furi_string_free(config->chainload_args);
    furi_string_free(config->chainload_file);
    furi_string_free(config->network_interface);

    free(config);
}

void config_copy(Boot2FlipperConfig* dest, const Boot2FlipperConfig* src) {
    if(dest == NULL || src == NULL) return;

    dest->dhcp = src->dhcp;
    furi_string_set(dest->ip_addr, src->ip_addr);
    furi_string_set(dest->subnet_mask, src->subnet_mask);
    furi_string_set(dest->gateway, src->gateway);
    furi_string_set(dest->dns, src->dns);
    furi_string_set(dest->chainload_url, src->chainload_url);
    furi_string_set(dest->chainload_args, src->chainload_args);
    furi_string_set(dest->chainload_file, src->chainload_file);
    dest->chainload_target = src->chainload_target;
    furi_string_set(dest->network_interface, src->network_interface);
    dest->partition_scheme = src->partition_scheme;
    dest->status_report = src->status_report;
    dest->b2f_quiet = src->b2f_quiet;
    dest->collect_sysinfo = src->collect_sysinfo;
    dest->write_log = src->write_log;
}

bool config_save(Storage* storage, const Boot2FlipperConfig* config, const char* file_path) {
    if(storage == NULL || config == NULL || file_path == NULL) {
        B2F_LOG_E(TAG, "Invalid parameters for config_save");
        return false;
    }

    // Ensure the directory exists
    storage_common_mkdir(storage, CONFIG_DIR_PATH);

    // Create FlipperFormat instance
    FlipperFormat* file = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        // Open file for write
        if(!flipper_format_file_open_always(file, file_path)) {
            B2F_LOG_E(TAG, "Failed to open file for writing: %s", file_path);
            break;
        }

        // Write header
        if(!flipper_format_write_header_cstr(file, "Boot2Flipper Config", 1)) {
            B2F_LOG_E(TAG, "Failed to write header");
            break;
        }

        // Write DHCP setting
        if(!flipper_format_write_bool(file, "DHCP", &config->dhcp, 1)) {
            B2F_LOG_E(TAG, "Failed to write DHCP");
            break;
        }

        // Write network settings (only meaningful if not DHCP, but save anyway)
        if(!flipper_format_write_string(file, "IP_Address", config->ip_addr)) {
            B2F_LOG_E(TAG, "Failed to write IP address");
            break;
        }

        if(!flipper_format_write_string(file, "Subnet_Mask", config->subnet_mask)) {
            B2F_LOG_E(TAG, "Failed to write subnet mask");
            break;
        }

        if(!flipper_format_write_string(file, "Gateway", config->gateway)) {
            B2F_LOG_E(TAG, "Failed to write gateway");
            break;
        }

        if(!flipper_format_write_string(file, "DNS", config->dns)) {
            B2F_LOG_E(TAG, "Failed to write DNS");
            break;
        }

        // Write chainload URL
        if(!flipper_format_write_string(file, "Chainload_URL", config->chainload_url)) {
            B2F_LOG_E(TAG, "Failed to write chainload URL");
            break;
        }

        // Write network interface
        if(!flipper_format_write_string(file, "Network_Interface", config->network_interface)) {
            B2F_LOG_E(TAG, "Failed to write network interface");
            break;
        }

        // Write partition scheme
        uint32_t partition_scheme = (uint32_t)config->partition_scheme;
        if(!flipper_format_write_uint32(file, "Partition_Scheme", &partition_scheme, 1)) {
            B2F_LOG_E(TAG, "Failed to write partition scheme");
            break;
        }

        // Write chainload target, arguments and file
        uint32_t chainload_target = (uint32_t)config->chainload_target;
        if(!flipper_format_write_uint32(file, "Chainload_Target", &chainload_target, 1) ||
           !flipper_format_write_string(file, "Chainload_Args", config->chainload_args) ||
           !flipper_format_write_string(file, "Chainload_File", config->chainload_file)) {
            B2F_LOG_E(TAG, "Failed to write chainload target");
            break;
        }

        if(!flipper_format_write_bool(file, "Status_Report", &config->status_report, 1) ||
           !flipper_format_write_bool(file, "B2F_Quiet", &config->b2f_quiet, 1) ||
           !flipper_format_write_bool(file, "Collect_Sysinfo", &config->collect_sysinfo, 1) ||
           !flipper_format_write_bool(file, "Write_Log", &config->write_log, 1)) {
            B2F_LOG_E(TAG, "Failed to write advanced settings");
            break;
        }

        success = true;
        B2F_LOG_I(TAG, "Configuration saved successfully to %s", file_path);

    } while(false);

    flipper_format_file_close(file);
    flipper_format_free(file);

    return success;
}

bool config_load(Storage* storage, Boot2FlipperConfig* config, const char* file_path) {
    if(storage == NULL || config == NULL || file_path == NULL) {
        B2F_LOG_E(TAG, "Invalid parameters for config_load");
        return false;
    }

    FlipperFormat* file = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        // Open file for read
        if(!flipper_format_file_open_existing(file, file_path)) {
            B2F_LOG_E(TAG, "Failed to open file for reading: %s", file_path);
            break;
        }

        // Read and verify header
        FuriString* header = furi_string_alloc();
        uint32_t version;

        if(!flipper_format_read_header(file, header, &version)) {
            B2F_LOG_E(TAG, "Failed to read header");
            furi_string_free(header);
            break;
        }

        if(furi_string_cmp_str(header, "Boot2Flipper Config") != 0) {
            B2F_LOG_E(TAG, "Invalid file format");
            furi_string_free(header);
            break;
        }

        furi_string_free(header);

        // Read DHCP setting
        if(!flipper_format_read_bool(file, "DHCP", &config->dhcp, 1)) {
            B2F_LOG_E(TAG, "Failed to read DHCP setting");
            break;
        }

        // Read network settings
        if(!flipper_format_read_string(file, "IP_Address", config->ip_addr)) {
            B2F_LOG_E(TAG, "Failed to read IP address");
            break;
        }

        if(!flipper_format_read_string(file, "Subnet_Mask", config->subnet_mask)) {
            B2F_LOG_E(TAG, "Failed to read subnet mask");
            break;
        }

        if(!flipper_format_read_string(file, "Gateway", config->gateway)) {
            B2F_LOG_E(TAG, "Failed to read gateway");
            break;
        }

        if(!flipper_format_read_string(file, "DNS", config->dns)) {
            B2F_LOG_E(TAG, "Failed to read DNS");
            break;
        }

        // Read chainload URL
        if(!flipper_format_read_string(file, "Chainload_URL", config->chainload_url)) {
            B2F_LOG_E(TAG, "Failed to read chainload URL");
            break;
        }

        // Read network interface (optional for backward compatibility)
        if(!flipper_format_read_string(file, "Network_Interface", config->network_interface)) {
            B2F_LOG_W(TAG, "Network interface not found, using default (net0)");
            furi_string_set(config->network_interface, "net0");
        }

        // Read partition scheme (optional for backward compatibility)
        uint32_t partition_scheme = (uint32_t)PARTITION_SCHEME_HYBRID;
        if(flipper_format_read_uint32(file, "Partition_Scheme", &partition_scheme, 1) &&
           partition_scheme < PARTITION_SCHEME_COUNT) {
            config->partition_scheme = (PartitionScheme)partition_scheme;
        } else {
            B2F_LOG_W(TAG, "Partition scheme missing or invalid, using default (Hybrid)");
            config->partition_scheme = PARTITION_SCHEME_HYBRID;
        }

        // Read chainload target (optional, older files have Chainload_Enabled)
        uint32_t chainload_target = CHAINLOAD_TARGET_URL;
        bool chainload_enabled = true;
        // Keys are looked up from the current position, so rewind for optional ones
        flipper_format_rewind(file);
        if(flipper_format_read_uint32(file, "Chainload_Target", &chainload_target, 1) &&
           chainload_target < CHAINLOAD_TARGET_COUNT) {
            config->chainload_target = (ChainloadTarget)chainload_target;
        } else if(
            flipper_format_rewind(file) &&
            flipper_format_read_bool(file, "Chainload_Enabled", &chainload_enabled, 1)) {
            config->chainload_target = chainload_enabled ? CHAINLOAD_TARGET_URL :
                                                           CHAINLOAD_TARGET_SHELL;
        } else {
            config->chainload_target = CHAINLOAD_TARGET_URL;
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_string(file, "Chainload_Args", config->chainload_args)) {
            furi_string_reset(config->chainload_args);
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_string(file, "Chainload_File", config->chainload_file)) {
            furi_string_reset(config->chainload_file);
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_bool(file, "Status_Report", &config->status_report, 1)) {
            config->status_report = true;
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_bool(file, "B2F_Quiet", &config->b2f_quiet, 1)) {
            config->b2f_quiet = true;
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_bool(file, "Collect_Sysinfo", &config->collect_sysinfo, 1)) {
            config->collect_sysinfo = false;
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_bool(file, "Write_Log", &config->write_log, 1)) {
            config->write_log = false;
        }

        success = true;
        B2F_LOG_I(TAG, "Configuration loaded successfully from %s", file_path);

    } while(false);

    flipper_format_file_close(file);
    flipper_format_free(file);

    return success;
}

bool config_load_last(Storage* storage, Boot2FlipperConfig* config, FuriString* file_path) {
    furi_string_set_str(file_path, CONFIG_FILE_PATH);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CONFIG_LAST_PROFILE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char path[256];
        size_t size = storage_file_read(file, path, sizeof(path) - 1);
        path[size] = '\0';
        if(size > 0) furi_string_set_str(file_path, path);
    }
    storage_file_free(file);

    // Loaded into a copy, a failed load leaves the configuration as is
    Boot2FlipperConfig* loaded = config_alloc();
    bool success = config_load(storage, loaded, furi_string_get_cstr(file_path));
    if(!success && furi_string_cmp_str(file_path, CONFIG_FILE_PATH) != 0) {
        furi_string_set_str(file_path, CONFIG_FILE_PATH);
        success = config_load(storage, loaded, CONFIG_FILE_PATH);
    }
    if(success) config_copy(config, loaded);
    config_free(loaded);
    return success;
}

void config_set_last(Storage* storage, const char* file_path) {
    storage_common_mkdir(storage, CONFIG_DIR_PATH);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CONFIG_LAST_PROFILE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, file_path, strlen(file_path));
    }
    storage_file_free(file);
}

void config_get_profile_name(const char* file_path, FuriString* name) {
    const char* slash = strrchr(file_path, '/');
    furi_string_set_str(name, slash ? slash + 1 : file_path);
    size_t dot = furi_string_search_rchar(name, '.', 0);
    if(dot != FURI_STRING_FAILURE && dot > 0) furi_string_left(name, dot);
}
