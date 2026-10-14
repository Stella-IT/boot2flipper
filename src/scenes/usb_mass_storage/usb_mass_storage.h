#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/widget.h>
#include <gui/modules/submenu.h>
#include "../../views/url_input.h"
#include "../../views/ip_input.h"
#include "../../views/confirm_input.h"
#include "../../disk/virtual_fat.h"
#include "../../b2f/b2f_dir.h"
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

// Views for prompts from the host (.B2F/prompt/)
typedef enum {
    USB_MASS_STORAGE_VIEW_PROMPT_CHOICE = 300,
    USB_MASS_STORAGE_VIEW_PROMPT_TEXT = 301,
    USB_MASS_STORAGE_VIEW_PROMPT_IP = 302,
    USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM = 304,
} UsbMassStorageView;

#define USB_MASS_STORAGE_PROMPT_TEXT_SIZE 128

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
    B2fDir* b2f;

    // Prompt from the host
    Submenu* prompt_choice;
    UrlInput* prompt_text;
    IpInput* prompt_ip;
    ConfirmInput* prompt_confirm;
    bool prompt_views_added;
    uint32_t prompt_view; // Shown prompt view, 0 if none
    B2fPrompt prompt;
    char prompt_title[B2F_PROMPT_OPTION_LENGTH * 2]; // First line of the question
    char prompt_header[B2F_PROMPT_OPTION_LENGTH * 2 + 16]; // Title and " (seconds)"
    uint32_t prompt_deadline; // Tick of the timeout, if the prompt has one
    int32_t prompt_seconds; // Countdown shown, -1 before the first update
    bool prompt_touched; // The user pressed a key, the timeout no longer runs
    char prompt_text_buffer[USB_MASS_STORAGE_PROMPT_TEXT_SIZE];
    uint8_t prompt_ip_buffer[4];
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
