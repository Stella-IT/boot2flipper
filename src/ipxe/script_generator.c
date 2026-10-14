#include "script_generator.h"
#include <stdarg.h>
#include <string.h>
#include "../b2f/b2f_dir.h"
#include "../log.h"

#define TAG "iPXEScript"

// Seconds to pick an interface on the Flipper before using all of them
#define INTERFACE_PROMPT_TIMEOUT 30

// Sets ${b2f} to the .B2F writer: b2f.efi on the disk on UEFI, the BIOS
// loader's initrd (b2f.pxe) on BIOS, which an imgfree of all images removes.
// Without a writer it names a missing file, so imgexec fails: left empty,
// imgexec would run the current script again.
static void append_b2f_setup(FuriString* script, bool bios_writer) {
    furi_string_cat_printf(
        script,
        "# Flipper: imgexec ${b2f} status|frame|mode|clear|question|options|kbdtype|timeout|prompt\n"
        "# Answer of prompt: ${${b2f_answer}}, see the Boot2Flipper README\n"
        "set b2f file:/%s/unavailable\n"
        "set b2f_answer b2f_result\n",
        B2F_BIN_PATH);
    // UEFI runs b2f.efi from the disk each time, so it still works after an imgfree
    furi_string_cat_printf(
        script,
        "iseq ${platform} efi && set b2f file:/%s && set b2f_answer efi/B2fAnswer:string || "
        "echo -n\n",
        B2F_EFI_PATH);
    // iPXE runs a PXE image (b2f.pxe) only with an open interface
    if(bios_writer) {
        furi_string_cat_printf(
            script, "iseq ${platform} pcbios && set b2f <INITRD> && ifopen || echo -n\n");
    }
}

// Shows a boot step on the Flipper if status_report is on
static void append_status(
    FuriString* script,
    const Boot2FlipperConfig* config,
    const char* b2f,
    const char* format,
    ...) {
    if(!config->status_report) return;
    furi_string_cat_printf(script, "%s status ", b2f);
    va_list args;
    va_start(args, format);
    furi_string_cat_vprintf(script, format, args);
    va_end(args);
    furi_string_cat_printf(script, " || echo -n\n");
}

// Writes a line to the Flipper's log file if write_log is on
static void append_log(
    FuriString* script,
    const Boot2FlipperConfig* config,
    const char* b2f,
    const char* format,
    ...) {
    if(!config->write_log) return;
    furi_string_cat_printf(script, "%s log ", b2f);
    va_list args;
    va_start(args, format);
    furi_string_cat_vprintf(script, format, args);
    va_end(args);
    furi_string_cat_printf(script, " || echo -n\n");
}

// Logs every interface, as configured, and the global network settings
static void append_network_log(FuriString* script, const char* b2f) {
    furi_string_cat_printf(
        script,
        "set b2f_n:int32 0\n"
        ":b2f_log_if\n"
        "isset ${net${b2f_n}/mac} || goto b2f_log_done\n"
        "%s log net${b2f_n}: mac=${net${b2f_n}/mac} chip=${net${b2f_n}/chip} "
        "bus=${net${b2f_n}/busloc} ip=${net${b2f_n}/ip} netmask=${net${b2f_n}/netmask} "
        "gateway=${net${b2f_n}/gateway} || echo -n\n"
        "inc b2f_n\n"
        "goto b2f_log_if\n"
        ":b2f_log_done\n"
        "%s log dns=${dns} domain=${domain} hostname=${hostname} "
        "dhcp-server=${dhcp-server} next-server=${next-server} filename=${filename} "
        "|| echo -n\n",
        b2f,
        b2f);
}

// Runs iPXE commands typed on the Flipper until the user cancels. BIOS runs
// them with the writer, UEFI iPXE chains them as a script the writer puts on
// the disk.
static void append_flipper_shell(FuriString* script, const char* b2f, bool quiet) {
    furi_string_cat_printf(
        script,
        "set b2f_q iPXE command\n"
        ":b2f_fsh\n"
        "%s question ${b2f_q} || goto b2f_failed\n"
        "%s options || goto b2f_failed\n"
        "%s kbdtype text || goto b2f_failed\n"
        "%s timeout 0 || goto b2f_failed\n"
        "%s prompt text || goto b2f_fsh_end\n"
        "iseq ${platform} efi && goto b2f_fsh_efi || echo -n\n"
        "%s exec && set b2f_q OK: ${${b2f_answer}} || set b2f_q Failed: ${${b2f_answer}}\n"
        "goto b2f_fsh\n"
        ":b2f_fsh_efi\n"
        "%s script && chain --autofree%s file:/%s/script && set b2f_q OK: ${${b2f_answer}} || "
        "set b2f_q Failed: ${${b2f_answer}}\n"
        "goto b2f_fsh\n"
        ":b2f_fsh_end\n"
        "shell\n"
        "goto b2f_end\n",
        b2f,
        b2f,
        b2f,
        b2f,
        b2f,
        b2f,
        b2f,
        quiet ? " -q" : "",
        B2F_PROMPT_PATH);
}

// Lists the interfaces as "netN (UP|DOWN, mac)" and asks on the Flipper which
// one to use; ${b2f_iface} stays unset (all interfaces) if not answered
static void append_interface_prompt(FuriString* script, const char* b2f) {
    furi_string_cat_printf(
        script,
        "set b2f_n:int32 0\n"
        ":b2f_if_list\n"
        "isset ${net${b2f_n}/mac} || goto b2f_if_ask\n"
        "iflinkwait --timeout 1500 net${b2f_n} && set b2f_link UP || set b2f_link DOWN\n"
        "set b2f_if${b2f_n} net${b2f_n} (${b2f_link}, ${net${b2f_n}/mac})\n"
        "isset ${b2f_ifs} && set b2f_ifs ${b2f_ifs}|${b2f_if${b2f_n}} || "
        "set b2f_ifs ${b2f_if${b2f_n}}\n"
        "inc b2f_n\n"
        "goto b2f_if_list\n"
        ":b2f_if_ask\n"
        "%s question Network interface? || goto b2f_if_done\n"
        "%s options ${b2f_ifs} || goto b2f_if_done\n"
        "%s timeout %d || goto b2f_if_done\n"
        "%s prompt choice || goto b2f_if_done\n"
        "set b2f_n:int32 0\n"
        ":b2f_if_pick\n"
        "iseq ${${b2f_answer}} ${b2f_if${b2f_n}} && goto b2f_if_found || inc b2f_n\n"
        "isset ${b2f_if${b2f_n}} && goto b2f_if_pick || goto b2f_if_done\n"
        ":b2f_if_found\n"
        "set b2f_iface net${b2f_n}\n"
        ":b2f_if_done\n"
        "%s timeout 0 || echo -n\n",
        b2f,
        b2f,
        b2f,
        INTERFACE_PROMPT_TIMEOUT,
        b2f,
        b2f);
}

FuriString* ipxe_script_generate(
    const Boot2FlipperConfig* config,
    bool bios_writer,
    const char* local_script) {
    FuriString* script = furi_string_alloc();
    const char* iface = furi_string_get_cstr(config->network_interface);
    // ${b2f} can't carry the -q flag, a variable expands to a single argument
    const char* b2f = config->b2f_quiet ? "imgexec -q ${b2f}" : "imgexec ${b2f}";

    furi_string_cat_printf(script, "#!ipxe\n# Boot2Flipper\n");
    append_b2f_setup(script, bios_writer);
    append_log(script, config, b2f, "iPXE ${version} on ${platform} ${buildarch}");
    if(config->collect_sysinfo) {
        furi_string_cat_printf(
            script,
            "%s sysinfo platform=${platform}\\narch=${buildarch}\\nmanufacturer=${manufacturer}"
            "\\nproduct=${product}\\nserial=${serial}\\nuuid=${uuid} || echo -n\n",
            b2f);
    }

    // ${b2f_iface}: the interface to configure, unset for all of them
    if(strcmp(iface, NETWORK_INTERFACE_ASK) == 0) {
        append_status(script, config, b2f, "Looking for network interfaces");
        append_interface_prompt(script, b2f);
    } else if(iface[0] != '\0' && strcmp(iface, NETWORK_INTERFACE_AUTO) != 0) {
        furi_string_cat_printf(script, "set b2f_iface %s\n", iface);
    }

    if(config->dhcp) {
        furi_string_cat_printf(script, "echo Boot2Flipper: Configuring network (DHCP)\n");
        append_status(script, config, b2f, "Getting IP address (DHCP)");
        furi_string_cat_printf(
            script, "isset ${b2f_iface} && dhcp ${b2f_iface} || dhcp || goto b2f_failed\n");
    } else {
        // Static IP needs an interface
        const char* ip = furi_string_get_cstr(config->ip_addr);
        furi_string_cat_printf(script, "echo Boot2Flipper: Configuring network (Static IP)\n");
        append_status(script, config, b2f, "Setting IP address to %s", ip);
        furi_string_cat_printf(
            script,
            "isset ${b2f_iface} || set b2f_iface net0\n"
            "set ${b2f_iface}/ip %s\n"
            "set ${b2f_iface}/netmask %s\n"
            "set ${b2f_iface}/gateway %s\n"
            "set dns %s\n"
            "ifopen ${b2f_iface} || goto b2f_failed\n",
            ip,
            furi_string_get_cstr(config->subnet_mask),
            furi_string_get_cstr(config->gateway),
            furi_string_get_cstr(config->dns));
    }
    furi_string_cat_printf(script, "echo IP: ${ip}, gateway: ${gateway}, DNS: ${dns}\n");
    if(config->write_log) append_network_log(script, b2f);

    switch(config->chainload_target) {
    case CHAINLOAD_TARGET_URL: {
        const char* url = furi_string_get_cstr(config->chainload_url);
        const char* args = furi_string_get_cstr(config->chainload_args);
        furi_string_cat_printf(script, "echo Chainloading: %s\n", url);
        append_status(script, config, b2f, "IP: ${ip}, chainloading %s", url);
        append_log(script, config, b2f, "Chainloading %s %s", url, args);
        furi_string_cat_printf(
            script,
            "chain --autofree %s%s%s || goto b2f_failed\ngoto b2f_returned\n",
            url,
            args[0] ? " " : "",
            args);
        break;
    }
    case CHAINLOAD_TARGET_FILE: {
        const char* name = strrchr(furi_string_get_cstr(config->chainload_file), '/');
        name = name ? name + 1 : furi_string_get_cstr(config->chainload_file);
        append_status(script, config, b2f, "IP: ${ip}, running %s", name);
        append_log(script, config, b2f, "Running %s", name);
        if(local_script) {
            // Inline, the script's own "#!ipxe" line is a comment here
            furi_string_cat_printf(script, "%s\ngoto b2f_returned\n", local_script);
        } else {
            furi_string_cat_printf(
                script,
                "chain --autofree file:/%s || goto b2f_failed\ngoto b2f_returned\n",
                IPXE_SCRIPT_CHAIN_FILE);
        }
        break;
    }
    case CHAINLOAD_TARGET_FLIPPER_SHELL:
        append_status(script, config, b2f, "IP: ${ip}, Flipper shell");
        append_flipper_shell(script, b2f, config->b2f_quiet);
        break;
    default:
        append_status(script, config, b2f, "IP: ${ip}, iPXE shell");
        furi_string_cat_printf(script, "shell\ngoto b2f_end\n");
        break;
    }

    // A chained image or script that returns leaves iPXE in the shell too
    furi_string_cat_printf(script, ":b2f_returned\necho Returned. Dropping to shell\n");
    append_status(script, config, b2f, "Returned, iPXE shell");
    append_log(script, config, b2f, "Returned, dropping to the iPXE shell");
    furi_string_cat_printf(script, "shell\ngoto b2f_end\n");

    furi_string_cat_printf(script, ":b2f_failed\necho Failed. Dropping to shell\n");
    append_status(script, config, b2f, "Failed, iPXE shell");
    append_log(script, config, b2f, "Failed, dropping to the iPXE shell");
    furi_string_cat_printf(script, "shell\n:b2f_end\n");

    B2F_LOG_I(
        TAG,
        "Generated script, DHCP: %d, interface: %s, target: %d, size: %zu bytes",
        config->dhcp,
        iface,
        config->chainload_target,
        furi_string_size(script));
    return script;
}
