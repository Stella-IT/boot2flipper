#pragma once

#include <furi.h>

/**
 * iPXE script generator for Boot2Flipper
 * Generates iPXE boot scripts based on network configuration
 */

/**
 * Generate iPXE script for DHCP mode
 * @param chainload_url URL to chainload after network setup
 * @param network_interface Network interface name (e.g., "net0", "net1")
 * @param chainload_enabled Enable chainloading (if false, drops to shell after network setup)
 * @return Generated script as FuriString (caller must free)
 */
FuriString* ipxe_script_generate_dhcp(
    const char* chainload_url,
    const char* network_interface,
    bool chainload_enabled);

/**
 * Generate iPXE script for static IP mode
 * @param ip_addr IP address
 * @param subnet_mask Subnet mask
 * @param gateway Gateway address
 * @param dns DNS server address
 * @param chainload_url URL to chainload after network setup
 * @param network_interface Network interface name (e.g., "net0", "net1")
 * @param chainload_enabled Enable chainloading (if false, drops to shell after network setup)
 * @return Generated script as FuriString (caller must free)
 */
FuriString* ipxe_script_generate_static(
    const char* ip_addr,
    const char* subnet_mask,
    const char* gateway,
    const char* dns,
    const char* chainload_url,
    const char* network_interface,
    bool chainload_enabled);

/**
 * Get script size (useful for disk image calculation)
 * @param script Script string
 * @return Size in bytes
 */
size_t ipxe_script_get_size(const FuriString* script);
