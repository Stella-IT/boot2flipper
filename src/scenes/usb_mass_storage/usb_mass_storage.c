#include "../../main.h"
#include "usb_mass_storage.h"
#include "../../ipxe/script_generator.h"
#include "../../ipxe/ipxe_validator.h"
#include "../../disk/virtual_fat.h"
#include "../../usb/usb_scsi.h"
#include "../../usb/usb_msc.h"

#define THIS_SCENE UsbMassStorage

static void usb_mass_storage_draw_callback(Canvas* canvas, void* model) {
    AppUsbMassStorage** instance_ptr = (AppUsbMassStorage**)model;
    AppUsbMassStorage* instance = *instance_ptr;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(!instance) {
        canvas_draw_str(canvas, 10, 30, "ERROR: NULL instance");
        return;
    }

    switch(instance->state) {
    case UsbMassStorageStateIdle:
        canvas_draw_str(canvas, 10, 30, "Press OK to start");
        break;

    case UsbMassStorageStateStarting:
        canvas_draw_str(canvas, 10, 20, "Starting USB...");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 10, 35, "Generating disk image");
        break;

    case UsbMassStorageStateActive:
        canvas_draw_str(canvas, 10, 12, "Boot2Flipper Ready");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 10, 24, "Active and ready");

        // Display currently accessed file
        if(instance->current_file && furi_string_size(instance->current_file) > 0) {
            canvas_draw_str(canvas, 10, 36, "Reading:");
            canvas_set_font(canvas, FontSecondary);
            const char* filename = furi_string_get_cstr(instance->current_file);
            canvas_draw_str(canvas, 10, 46, filename);
        }

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 10, 58, "Press BACK to stop");
        break;

    case UsbMassStorageStateStopping:
        canvas_draw_str(canvas, 10, 30, "Stopping USB...");
        break;

    case UsbMassStorageStateMissingFile:
        canvas_draw_str(canvas, 10, 12, "Missing iPXE Files");
        canvas_set_font(canvas, FontSecondary);
        if(instance->status_text) {
            // Manually parse and draw multiline text
            const char* text = furi_string_get_cstr(instance->status_text);
            const char* line_start = text;
            uint8_t y = 24;

            while(*line_start != '\0' && y < 60) {
                const char* line_end = line_start;
                // Find end of line or end of string
                while(*line_end != '\0' && *line_end != '\n') {
                    line_end++;
                }

                // Draw line (max 64 chars to fit screen)
                size_t line_len = line_end - line_start;
                if(line_len > 0) {
                    char line_buffer[65];
                    size_t copy_len = line_len < 64 ? line_len : 64;
                    memcpy(line_buffer, line_start, copy_len);
                    line_buffer[copy_len] = '\0';
                    canvas_draw_str(canvas, 5, y, line_buffer);
                    y += 10;
                }

                // Move to next line
                line_start = (*line_end == '\n') ? line_end + 1 : line_end;
            }
        }
        break;

    case UsbMassStorageStateError:
        canvas_draw_str(canvas, 10, 20, "Error!");
        canvas_set_font(canvas, FontSecondary);
        if(instance->status_text) {
            canvas_draw_str(canvas, 10, 35, furi_string_get_cstr(instance->status_text));
        }
        canvas_draw_str(canvas, 10, 50, "Press BACK to exit");
        break;
    }
}

static void file_read_callback(const char* filename, void* context) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)context;
    if(instance && instance->current_file) {
        furi_string_set_str(instance->current_file, filename);
        // Trigger view update
        if(instance->view) {
            view_dispatcher_send_custom_event(((App*)instance->app)->view_dispatcher, 0xFF);
        }
    }
}

static bool usb_mass_storage_input_callback(InputEvent* event, void* context) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)context;

    if(!instance || !instance->app) {
        return false;
    }

    App* app = (App*)instance->app;

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(instance->state == UsbMassStorageStateIdle) {
            // Send custom event to scene manager to handle state change
            view_dispatcher_send_custom_event(app->view_dispatcher, 0x01);
            return true;
        }
    }

    return false;
}

AppUsbMassStorage* UsbMassStorage_alloc() {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)malloc(sizeof(AppUsbMassStorage));

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLockFree, sizeof(AppUsbMassStorage*));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, usb_mass_storage_draw_callback);
    view_set_input_callback(instance->view, usb_mass_storage_input_callback);

    // Store instance pointer in model for draw callback
    AppUsbMassStorage** model = view_get_model(instance->view);
    *model = instance;

    instance->app = NULL;
    instance->widget = NULL;
    instance->state = UsbMassStorageStateIdle;
    instance->usb_thread = NULL;
    instance->vfat = NULL;
    instance->scsi = NULL;
    instance->msc = NULL;

    instance->ip_addr = furi_string_alloc();
    instance->subnet_mask = furi_string_alloc();
    instance->gateway = furi_string_alloc();
    instance->dns = furi_string_alloc();
    instance->chainload_url = furi_string_alloc();
    instance->network_interface = furi_string_alloc();
    instance->status_text = furi_string_alloc();
    instance->current_file = furi_string_alloc();
    instance->chainload_enabled = true; // Default: enabled

    return instance;
}

void UsbMassStorage_free(void* ptr) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)ptr;

    if(instance == NULL) return;

    if(instance->msc != NULL) {
        usb_msc_stop(instance->msc);
        usb_msc_free(instance->msc);
    }

    if(instance->scsi != NULL) {
        usb_scsi_free(instance->scsi);
    }

    if(instance->vfat != NULL) {
        virtual_fat_free(instance->vfat);
    }

    furi_string_free(instance->ip_addr);
    furi_string_free(instance->subnet_mask);
    furi_string_free(instance->gateway);
    furi_string_free(instance->dns);
    furi_string_free(instance->chainload_url);
    furi_string_free(instance->network_interface);
    furi_string_free(instance->status_text);
    furi_string_free(instance->current_file);

    view_free(instance->view);

    free(instance);
}

View* UsbMassStorage_get_view(void* ptr) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)ptr;
    FURI_LOG_I("UsbMassStorage", "UsbMassStorage_get_view called, instance: %p", instance);
    if(instance == NULL) {
        FURI_LOG_E("UsbMassStorage", "instance is NULL in get_view!");
        return NULL;
    }
    FURI_LOG_I("UsbMassStorage", "instance->view: %p", instance->view);
    return instance->view;
}

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
    bool chainload_enabled) {
    instance->dhcp = dhcp;
    furi_string_set_str(instance->ip_addr, ip_addr);
    furi_string_set_str(instance->subnet_mask, subnet_mask);
    furi_string_set_str(instance->gateway, gateway);
    furi_string_set_str(instance->dns, dns);
    furi_string_set_str(instance->chainload_url, chainload_url);
    furi_string_set_str(instance->network_interface, network_interface);
    instance->partition_scheme = partition_scheme;
    instance->chainload_enabled = chainload_enabled;
}

void UsbMassStorage_on_enter(void* context) {
    App* app = (App*)context;
    AppUsbMassStorage* instance = app->allocated_scenes[THIS_SCENE];

    instance->app = app;
    instance->state = UsbMassStorageStateIdle;
    furi_string_reset(instance->current_file);

    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool UsbMassStorage_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    AppUsbMassStorage* instance = app->allocated_scenes[THIS_SCENE];

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == 0xFF) { // File read notification
            // Just trigger view redraw
            view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
            return true;
        } else if(event.event == 0x01) { // OK button pressed
            instance->state = UsbMassStorageStateStarting;
            view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);

            // 1. Validate iPXE binaries
            Storage* storage = furi_record_open(RECORD_STORAGE);
            IpxeValidationResult validation;

            if(!ipxe_validate_binaries(storage, &validation)) {
                FuriString* status = ipxe_get_status_message(&validation);
                furi_string_set(instance->status_text, status);
                furi_string_free(status);
                instance->state = UsbMassStorageStateMissingFile;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // 2. Generate iPXE script
            FuriString* ipxe_script = NULL;
            if(instance->dhcp) {
                ipxe_script = ipxe_script_generate_dhcp(
                    furi_string_get_cstr(instance->chainload_url),
                    furi_string_get_cstr(instance->network_interface),
                    instance->chainload_enabled);
            } else {
                ipxe_script = ipxe_script_generate_static(
                    furi_string_get_cstr(instance->ip_addr),
                    furi_string_get_cstr(instance->subnet_mask),
                    furi_string_get_cstr(instance->gateway),
                    furi_string_get_cstr(instance->dns),
                    furi_string_get_cstr(instance->chainload_url),
                    furi_string_get_cstr(instance->network_interface),
                    instance->chainload_enabled);
            }

            if(!ipxe_script) {
                furi_string_set(instance->status_text, "Failed to generate script");
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // 3. Create virtual FAT filesystem
            instance->vfat = virtual_fat_alloc();

            // Set partition scheme from config
            virtual_fat_set_partition_scheme(instance->vfat, instance->partition_scheme);

            // Register file read callback
            virtual_fat_set_read_callback(instance->vfat, file_read_callback, instance);

            // CONVERT TO c strings
            const char* ipxe_script_cstr = furi_string_get_cstr(ipxe_script);

            // Add iPXE script as AUTOEXEC.IPXE in root
            if(!virtual_fat_add_text_file(instance->vfat, "AUTOEXEC.IPXE", ipxe_script_cstr)) {
                furi_string_set(instance->status_text, "Failed to add AUTOEXEC.IPXE");
                furi_string_free(ipxe_script);
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // Add iPXE script as BOOT.CFG in root
            if(!virtual_fat_add_text_file(instance->vfat, "BOOT.CFG", ipxe_script_cstr)) {
                furi_string_set(instance->status_text, "Failed to add BOOT.CFG");
                furi_string_free(ipxe_script);
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }
            furi_string_free(ipxe_script);

            // Add BIOS iPXE (IPXE.LKR) in root
            if(!virtual_fat_add_sd_file(storage, instance->vfat, "IPXE.LKR", IPXE_BIOS_PATH)) {
                furi_string_set(instance->status_text, "Failed to add IPXE.LKR");
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // Add UEFI iPXE (BOOTX64.EFI) in EFI/BOOT/
            if(!virtual_fat_add_file_to_subdir(
                   storage, instance->vfat, "EFI/BOOT", "BOOTX64.EFI", IPXE_UEFI_PATH)) {
                furi_string_set(instance->status_text, "Failed to add BOOTX64.EFI");
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // 4. Initialize SCSI context
            instance->scsi = usb_scsi_alloc();
            usb_scsi_set_storage(instance->scsi, storage);
            usb_scsi_set_virtual_fat(instance->scsi, instance->vfat);

            // 5. Initialize and start USB MSC
            instance->msc = usb_msc_alloc();
            usb_msc_set_scsi(instance->msc, instance->scsi);

            if(!usb_msc_start(instance->msc)) {
                furi_string_set(instance->status_text, "Failed to start USB MSC");
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            furi_record_close(RECORD_STORAGE);

            // Success!
            instance->state = UsbMassStorageStateActive;
            view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);

            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(instance->state == UsbMassStorageStateActive) {
            // Stop USB MSC
            instance->state = UsbMassStorageStateStopping;
            view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);

            if(instance->msc) {
                usb_msc_stop(instance->msc);
                usb_msc_free(instance->msc);
                instance->msc = NULL;
            }

            if(instance->scsi) {
                usb_scsi_free(instance->scsi);
                instance->scsi = NULL;
            }

            if(instance->vfat) {
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
            }

            instance->state = UsbMassStorageStateIdle;
            return false; // Allow back navigation
        }

        if(instance->state == UsbMassStorageStateError ||
           instance->state == UsbMassStorageStateMissingFile ||
           instance->state == UsbMassStorageStateIdle) {
            return false; // Allow back navigation
        }
        return true; // Consume back in other states
    }

    return false;
}

void UsbMassStorage_on_exit(void* context) {
    App* app = (App*)context;
    AppUsbMassStorage* instance = app->allocated_scenes[THIS_SCENE];

    // Make sure USB is stopped
    if(instance->state == UsbMassStorageStateActive) {
        if(instance->msc) {
            usb_msc_stop(instance->msc);
        }
        instance->state = UsbMassStorageStateIdle;
    }
}
