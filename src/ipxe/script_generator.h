#pragma once

#include <furi.h>
#include "../config/config.h"

/**
 * iPXE script generator for Boot2Flipper
 * Generates iPXE boot scripts based on network configuration
 */

// Name of the CHAINLOAD_TARGET_FILE script on the disk
#define IPXE_SCRIPT_CHAIN_FILE "CHAIN.IPXE"

/**
 * Generate the iPXE boot script
 * @param config Network and chainload settings
 * @param bios_writer The BIOS loader passes the .B2F writer as initrd (the script is
 *        passed as command line), so ${b2f} can use it on BIOS
 * @param local_script For CHAINLOAD_TARGET_FILE: the script to run inline, for BIOS iPXE
 *        which can't read the disk. NULL chains IPXE_SCRIPT_CHAIN_FILE from the disk.
 * @return Generated script as FuriString (caller must free)
 */
FuriString* ipxe_script_generate(
    const Boot2FlipperConfig* config,
    bool bios_writer,
    const char* local_script);
