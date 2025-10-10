#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/widget.h>
#include "../../disk/virtual_fat.h"
#include "../../usb/usb_scsi.h"
#include "../../usb/usb_msc.h"

typedef enum {
    UsbMassStorageStateIdle,
    UsbMassStorageStateStarting,
    UsbMassStorageStateActive,
    UsbMassStorageStateStopping,
    UsbMassStorageStateMissingFile,
    UsbMassStorageStateError,
} UsbMassStorageState;

typedef struct {
    View* view;
    Widget* widget;
    UsbMassStorageState state;
    void* app; // Pointer to App for callbacks

    // Configuration from Home scene
    bool dhcp;
    FuriString* ip_addr;
    FuriString* subnet_mask;
    FuriString* gateway;
    FuriString* dns;
    FuriString* chainload_url;
    FuriString* network_interface;
    PartitionScheme partition_scheme;
    bool chainload_enabled;

    FuriThread* usb_thread;
    FuriString* status_text;
    FuriString* current_file;
    VirtualFat* vfat;
    UsbScsiContext* scsi;
    UsbMscContext* msc;
} AppUsbMassStorage;

AppUsbMassStorage* UsbMassStorage_alloc();
View* UsbMassStorage_get_view(void* p);
void UsbMassStorage_free(void* p);
void UsbMassStorage_on_enter(void* p);
bool UsbMassStorage_on_event(void* p, SceneManagerEvent e);
void UsbMassStorage_on_exit(void* p);

// Helper function to set configuration
void UsbMassStorage_set_config(
    AppUsbMassStorage* instance,
    bool dhcp,
    const char* ip_addr,
    const char* subnet_mask,
    const char* gateway,
    const char* dns,
    const char* chainload_url,
    const char* network_interface,
    PartitionScheme partition_scheme,
    bool chainload_enabled);
