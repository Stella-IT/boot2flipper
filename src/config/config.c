#include "config.h"
#include <storage/storage.h>

#define TAG "Boot2FlipperConfig"

Boot2FlipperConfig* config_alloc() {
    Boot2FlipperConfig* config = malloc(sizeof(Boot2FlipperConfig));

    config->dhcp = true;
    config->ip_addr = furi_string_alloc_set("192.168.1.10");
    config->subnet_mask = furi_string_alloc_set("255.255.255.0");
    config->gateway = furi_string_alloc_set("192.168.1.1");
    config->dns = furi_string_alloc_set("8.8.8.8");
    config->chainload_url = furi_string_alloc_set("http://boot.ipxe.org/demo/boot.php");
    config->network_interface = furi_string_alloc_set("auto"); // Default: auto-detect
    config->partition_scheme = PARTITION_SCHEME_GPT_ONLY; // Default: GPT (UEFI)
    config->chainload_enabled = true; // Default: chainloading enabled

    return config;
}

void config_free(Boot2FlipperConfig* config) {
    if(config == NULL) return;

    furi_string_free(config->ip_addr);
    furi_string_free(config->subnet_mask);
    furi_string_free(config->gateway);
    furi_string_free(config->dns);
    furi_string_free(config->chainload_url);
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
    furi_string_set(dest->network_interface, src->network_interface);
    dest->partition_scheme = src->partition_scheme;
    dest->chainload_enabled = src->chainload_enabled;
}

bool config_save(Storage* storage, const Boot2FlipperConfig* config, const char* file_path) {
    if(storage == NULL || config == NULL || file_path == NULL) {
        FURI_LOG_E(TAG, "Invalid parameters for config_save");
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
            FURI_LOG_E(TAG, "Failed to open file for writing: %s", file_path);
            break;
        }

        // Write header
        if(!flipper_format_write_header_cstr(file, "Boot2Flipper Config", 1)) {
            FURI_LOG_E(TAG, "Failed to write header");
            break;
        }

        // Write DHCP setting
        if(!flipper_format_write_bool(file, "DHCP", &config->dhcp, 1)) {
            FURI_LOG_E(TAG, "Failed to write DHCP");
            break;
        }

        // Write network settings (only meaningful if not DHCP, but save anyway)
        if(!flipper_format_write_string(file, "IP_Address", config->ip_addr)) {
            FURI_LOG_E(TAG, "Failed to write IP address");
            break;
        }

        if(!flipper_format_write_string(file, "Subnet_Mask", config->subnet_mask)) {
            FURI_LOG_E(TAG, "Failed to write subnet mask");
            break;
        }

        if(!flipper_format_write_string(file, "Gateway", config->gateway)) {
            FURI_LOG_E(TAG, "Failed to write gateway");
            break;
        }

        if(!flipper_format_write_string(file, "DNS", config->dns)) {
            FURI_LOG_E(TAG, "Failed to write DNS");
            break;
        }

        // Write chainload URL
        if(!flipper_format_write_string(file, "Chainload_URL", config->chainload_url)) {
            FURI_LOG_E(TAG, "Failed to write chainload URL");
            break;
        }

        // Write network interface
        if(!flipper_format_write_string(file, "Network_Interface", config->network_interface)) {
            FURI_LOG_E(TAG, "Failed to write network interface");
            break;
        }

        // Write partition scheme
        uint32_t partition_scheme = (uint32_t)config->partition_scheme;
        if(!flipper_format_write_uint32(file, "Partition_Scheme", &partition_scheme, 1)) {
            FURI_LOG_E(TAG, "Failed to write partition scheme");
            break;
        }

        // Write chainload enabled flag
        if(!flipper_format_write_bool(file, "Chainload_Enabled", &config->chainload_enabled, 1)) {
            FURI_LOG_E(TAG, "Failed to write chainload enabled flag");
            break;
        }

        success = true;
        FURI_LOG_I(TAG, "Configuration saved successfully to %s", file_path);

    } while(false);

    flipper_format_file_close(file);
    flipper_format_free(file);

    return success;
}

bool config_load(Storage* storage, Boot2FlipperConfig* config, const char* file_path) {
    if(storage == NULL || config == NULL || file_path == NULL) {
        FURI_LOG_E(TAG, "Invalid parameters for config_load");
        return false;
    }

    FlipperFormat* file = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        // Open file for read
        if(!flipper_format_file_open_existing(file, file_path)) {
            FURI_LOG_E(TAG, "Failed to open file for reading: %s", file_path);
            break;
        }

        // Read and verify header
        FuriString* header = furi_string_alloc();
        uint32_t version;

        if(!flipper_format_read_header(file, header, &version)) {
            FURI_LOG_E(TAG, "Failed to read header");
            furi_string_free(header);
            break;
        }

        if(furi_string_cmp_str(header, "Boot2Flipper Config") != 0) {
            FURI_LOG_E(TAG, "Invalid file format");
            furi_string_free(header);
            break;
        }

        furi_string_free(header);

        // Read DHCP setting
        if(!flipper_format_read_bool(file, "DHCP", &config->dhcp, 1)) {
            FURI_LOG_E(TAG, "Failed to read DHCP setting");
            break;
        }

        // Read network settings
        if(!flipper_format_read_string(file, "IP_Address", config->ip_addr)) {
            FURI_LOG_E(TAG, "Failed to read IP address");
            break;
        }

        if(!flipper_format_read_string(file, "Subnet_Mask", config->subnet_mask)) {
            FURI_LOG_E(TAG, "Failed to read subnet mask");
            break;
        }

        if(!flipper_format_read_string(file, "Gateway", config->gateway)) {
            FURI_LOG_E(TAG, "Failed to read gateway");
            break;
        }

        if(!flipper_format_read_string(file, "DNS", config->dns)) {
            FURI_LOG_E(TAG, "Failed to read DNS");
            break;
        }

        // Read chainload URL
        if(!flipper_format_read_string(file, "Chainload_URL", config->chainload_url)) {
            FURI_LOG_E(TAG, "Failed to read chainload URL");
            break;
        }

        // Read network interface (optional for backward compatibility)
        if(!flipper_format_read_string(file, "Network_Interface", config->network_interface)) {
            FURI_LOG_W(TAG, "Network interface not found, using default (net0)");
            furi_string_set(config->network_interface, "net0");
        }

        // Read partition scheme (optional for backward compatibility)
        uint32_t partition_scheme = (uint32_t)PARTITION_SCHEME_GPT_ONLY;
        if(flipper_format_read_uint32(file, "Partition_Scheme", &partition_scheme, 1)) {
            config->partition_scheme = (PartitionScheme)partition_scheme;
        } else {
            FURI_LOG_W(TAG, "Partition scheme not found, using default (GPT)");
            config->partition_scheme = PARTITION_SCHEME_GPT_ONLY;
        }

        // Read chainload enabled flag (optional for backward compatibility)
        if(!flipper_format_read_bool(file, "Chainload_Enabled", &config->chainload_enabled, 1)) {
            FURI_LOG_W(TAG, "Chainload enabled flag not found, using default (enabled)");
            config->chainload_enabled = true;
        }

        success = true;
        FURI_LOG_I(TAG, "Configuration loaded successfully from %s", file_path);

    } while(false);

    flipper_format_file_close(file);
    flipper_format_free(file);

    return success;
}
