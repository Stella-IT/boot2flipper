#include "script_generator.h"

#define TAG "iPXEScript"

FuriString* ipxe_script_generate_dhcp(
    const char* chainload_url,
    const char* network_interface,
    bool chainload_enabled) {
    FuriString* script = furi_string_alloc();

    // Determine interface - if blank/NULL, use auto-detect (no interface specified)
    // Otherwise use the specified interface
    bool has_interface = (network_interface && network_interface[0]);
    const char* iface = has_interface ? network_interface : "net0";

    if (!has_interface || strcmp(iface, "auto") == 0) {
        iface = ""; // Empty string for auto-detect
        has_interface = false;
    }

    // iPXE script for DHCP mode
    furi_string_cat_printf(script, "#!ipxe\n");
    furi_string_cat_printf(script, "# Boot2Flipper - DHCP Mode\n");
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, "echo Boot2Flipper: Configuring network (DHCP)\n");

    // Use "dhcp" for auto-detect, "dhcp <interface>" for specific interface
    if(has_interface) {
        furi_string_cat_printf(script, "dhcp %s || goto failed\n", iface);
        furi_string_cat_printf(script, "\n");
        furi_string_cat_printf(script, "echo Network configured:\n");
        furi_string_cat_printf(script, "echo IP: ${%s/ip}\n", iface);
        furi_string_cat_printf(script, "echo Gateway: ${%s/gateway}\n", iface);
        furi_string_cat_printf(script, "echo DNS: ${%s/dns}\n", iface);
    } else {
        furi_string_cat_printf(script, "dhcp || goto failed\n");
    }
    furi_string_cat_printf(script, "\n");

    if(chainload_enabled) {
        furi_string_cat_printf(script, "echo Chainloading: %s\n", chainload_url);
        furi_string_cat_printf(script, "chain --autofree %s || goto failed\n", chainload_url);
    } else {
        furi_string_cat_printf(script, "echo Network configured successfully\n");
        furi_string_cat_printf(script, "echo Chainloading disabled, dropping to shell\n");
        furi_string_cat_printf(script, "shell\n");
        furi_string_cat_printf(script, "goto end\n");
    }

    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, ":failed\n");
    furi_string_cat_printf(script, "echo Dropping to shell\n");
    furi_string_cat_printf(script, "shell\n");
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, ":end\n");

    if(has_interface) {
        FURI_LOG_I(
            TAG,
            "Generated DHCP script for %s, chainload: %s, size: %zu bytes",
            iface,
            chainload_enabled ? "enabled" : "disabled",
            furi_string_size(script));
    } else {
        FURI_LOG_I(
            TAG,
            "Generated DHCP script (auto-detect), chainload: %s, size: %zu bytes",
            chainload_enabled ? "enabled" : "disabled",
            furi_string_size(script));
    }
    return script;
}

FuriString* ipxe_script_generate_static(
    const char* ip_addr,
    const char* subnet_mask,
    const char* gateway,
    const char* dns,
    const char* chainload_url,
    const char* network_interface,
    bool chainload_enabled) {
    FuriString* script = furi_string_alloc();

    // Use default if network_interface is NULL
    const char* iface = has_interface ? network_interface : "net0";

    if (!has_interface || strcmp(iface, "auto") == 0) {
        iface = "net0";
    }

    // Calculate netmask bits from subnet mask (e.g., 255.255.255.0 -> 24)
    // For simplicity, we'll use the dotted notation directly
    // iPXE supports both CIDR and dotted notation

    // iPXE script for static IP mode
    furi_string_cat_printf(script, "#!ipxe\n");
    furi_string_cat_printf(script, "# Boot2Flipper - Static IP Mode\n");
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, "echo Boot2Flipper: Configuring network (Static IP)\n");
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, "# Configure static IP\n");
    furi_string_cat_printf(script, "set %s/ip %s\n", iface, ip_addr);
    furi_string_cat_printf(script, "set %s/netmask %s\n", iface, subnet_mask);
    furi_string_cat_printf(script, "set %s/gateway %s\n", iface, gateway);
    furi_string_cat_printf(script, "set dns %s\n", dns);
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, "# Open network interface\n");
    furi_string_cat_printf(script, "ifopen %s || goto failed\n", iface);
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, "echo Network configured:\n");
    furi_string_cat_printf(script, "echo IP: ${%s/ip}\n", iface);
    furi_string_cat_printf(script, "echo Netmask: ${%s/netmask}\n", iface);
    furi_string_cat_printf(script, "echo Gateway: ${%s/gateway}\n", iface);
    furi_string_cat_printf(script, "echo DNS: ${dns}\n");
    furi_string_cat_printf(script, "\n");

    if(chainload_enabled) {
        furi_string_cat_printf(script, "echo Chainloading: %s\n", chainload_url);
        furi_string_cat_printf(script, "chain --autofree %s || goto failed\n", chainload_url);
    } else {
        furi_string_cat_printf(script, "echo Network configured successfully\n");
        furi_string_cat_printf(script, "echo Chainloading disabled, dropping to shell\n");
        furi_string_cat_printf(script, "shell\n");
        furi_string_cat_printf(script, "goto end\n");
    }

    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, ":failed\n");
    furi_string_cat_printf(script, "echo Dropping to shell\n");
    furi_string_cat_printf(script, "shell\n");
    furi_string_cat_printf(script, "\n");
    furi_string_cat_printf(script, ":end\n");

    FURI_LOG_I(
        TAG,
        "Generated static IP script for %s, chainload: %s, size: %zu bytes",
        iface,
        chainload_enabled ? "enabled" : "disabled",
        furi_string_size(script));
    return script;
}

size_t ipxe_script_get_size(const FuriString* script) {
    return furi_string_size(script);
}
