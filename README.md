<h1 align="center">boot2flipper</h1>
<p align="center">Use your Flipper Zero as PXE Recovery Disk</p>

> [!WARNING]
> This application is not an official Stella IT product. Use at your own risk.

## What is this?
Boot2Flipper is an application for Flipper Zero that allows you to emulate a iPXE USB thumb drive.  

## Features
* Automatic `autoexec.ipxe` script generation
  - Configure IP address, netmask, gateway and DNS server on your Flipper Zero
  - Automatically chainload to specified URL
* UEFI and Legacy BIOS support
* Config Load/Save to/from SD card
* Support for custom iPXE EFI executable

### How does it work?
Boot2Flipper emulates a USB Mass Storage device with "virtual" FAT32 filesystem.
The Boot2Flipper application generates FAT32 File Allocation Table and `autoexec.ipxe` script and iPXE EFI Executable on the fly, with MBR of iPXE.  

When the file itself is requested, Boot2Flipper automatically calculates the offset from the each file's cluster number and returns the file content from the underlying flipper filesystem.  

### How to setup iPXE files?
1. Install Boot2Flipper on your Flipper Zero
2. Open `Boot2Flipper` application on your Flipper Zero
3. Open `qFlipper` or other file transfer application (e.g. [`f0-mtp`](https://github.com/Alex4386/f0-mtp))
4. Head to `SD Card` -> `apps_data` -> `boot2flipper`
5. Create `ipxe` directory
6. Download `ipxe.efi` and `ipxe.lkrn` files from [boot.ipxe.org](https://boot.ipxe.org) and put them into `ipxe` directory

### How to use Boot2Flipper?
1. Open `Boot2Flipper` application on your Flipper Zero
2. (Optional) Load configuration from SD card
3. Configure Network (DHCP or Static IP)
4. (Static IP only) 
   1. configure IP address, netmask, gateway and DNS server at `NetworkSettings` menu.
   2. (Static IP Only) Configure Network Interface other than `auto` (If it is left to `auto`, it will automatically use `net0` interface)
5. Select `Boot Method`. `MBR` or `UEFI`
6. If you want to drop to iPXE shell, Set Chainload to `Disabled`.
   1. Else, Set the ChainloadURL to your desired URL.
7. (Optional) Save configuration to SD card
8. Press `Start` button to start emulating USB Mass Storage device
9. On your PC, Select Boot Device labelled as `FLIPPER Boot2Flipper 1.0` or similar.
10. Your Flipper Zero will report as it is reading `ipxe.lkrn` or `bootx64.efi` file., due to flipper zero's limitation, it will take some time to read and send the file to PC.
11. Congratulations, You'll see iPXE booting up on your PC!

## Build Status

<!-- Replace the https://github.com/Alex4386/f0-template to your own repo after using template! -->

- **Latest Release**: [Download](https://github.com/Stella-IT/boot2flipper/releases/latest)
- **Latest Nightly**: [Download](https://github.com/Stella-IT/boot2flipper/actions/workflows/nightly.yml) _(GitHub Login Required)_

|                                            Nightly Build                                            |                                            Release Build                                            |
| :-------------------------------------------------------------------------------------------------: | :-----------------------------------------------------------------------------------------------: |
| ![Nightly Build](https://github.com/Stella-IT/boot2flipper/actions/workflows/nightly.yml/badge.svg) | ![Release Build](https://github.com/Stella-IT/boot2flipper/actions/workflows/release.yml/badge.svg) |

## Setup Development Environment
See [DEVELOPMENT.md](DEVELOPMENT.md) to see how to setup your development environment.

## FAQ
1. **Why didn't you use other sizes less than `128MB`?**  
   This is due to some BIOSes not respecting `ESP` partition sizes that are less than `100MB`.
   This is a known issue with some UEFI implementations. Due to this, if you have Debug logging enabled in Flipper Zero, initial FAT32 FAT scan may take a while. So either disable Debug logging or wait.

