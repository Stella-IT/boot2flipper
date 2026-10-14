#include "../../main.h"
#include "usb_mass_storage.h"
#include "../../ipxe/script_generator.h"
#include "../../ipxe/ipxe_validator.h"
#include "../../disk/virtual_fat.h"
#include "../../disk/bios_boot.h"
#include "../../usb/usb_scsi.h"
#include "../../usb/usb_msc.h"
#include "../../log.h"

#define THIS_SCENE UsbMassStorage

#define EVENT_FILE_READ 0xFF
#define EVENT_START     0x01
#define EVENT_PROMPT    0x10 // The host asked a prompt

#define STATUS_LINES       3
#define STATUS_LINE_HEIGHT 10

// Status text set by the host through .B2F/status, one line per '\n'
static void usb_mass_storage_draw_status(Canvas* canvas, const char* text) {
    uint8_t y = 24;
    for(uint8_t line = 0; line < STATUS_LINES && *text != '\0'; line++) {
        const char* end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);

        char buffer[64];
        if(length >= sizeof(buffer)) length = sizeof(buffer) - 1;
        memcpy(buffer, text, length);
        buffer[length] = '\0';
        while(length > 0 && canvas_string_width(canvas, buffer) > 116) {
            buffer[--length] = '\0';
        }
        canvas_draw_str(canvas, 10, y, buffer);

        y += STATUS_LINE_HEIGHT;
        text = end ? end + 1 : text + strlen(text);
    }
}

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

    case UsbMassStorageStateActive: {
        // The host's frame takes the whole screen in framebuffer mode
        if(instance->b2f && b2f_dir_get_mode(instance->b2f) == B2fModeFramebuffer) {
            canvas_draw_xbm(canvas, 0, 0, 128, 64, b2f_dir_acquire_frame(instance->b2f));
            b2f_dir_release_frame(instance->b2f);
            break;
        }

        canvas_draw_str(canvas, 10, 12, "Boot2Flipper Ready");
        canvas_set_font(canvas, FontSecondary);

        // Status from the host replaces the file being read
        char status[STATUS_LINES * 64] = "";
        if(instance->b2f && b2f_dir_get_status(instance->b2f, status, sizeof(status)) > 0) {
            usb_mass_storage_draw_status(canvas, status);
        } else {
            canvas_draw_str(canvas, 10, 24, "Active and ready");
        }

        // Display currently accessed file
        if(instance->current_file && furi_string_size(instance->current_file) > 0 &&
           status[0] == '\0') {
            canvas_draw_str(canvas, 10, 36, "Reading:");
            canvas_set_font(canvas, FontSecondary);
            const char* filename = furi_string_get_cstr(instance->current_file);
            canvas_draw_str(canvas, 10, 46, filename);
        }

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 10, 58, "Press BACK to stop");
        break;
    }

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
            view_dispatcher_send_custom_event(
                ((App*)instance->app)->view_dispatcher, EVENT_FILE_READ);
        }
    }
}

static FuriString*
    usb_mass_storage_generate_script(AppUsbMassStorage* instance, bool bios_writer) {
    if(instance->dhcp) {
        return ipxe_script_generate_dhcp(
            furi_string_get_cstr(instance->chainload_url),
            furi_string_get_cstr(instance->network_interface),
            instance->chainload_enabled,
            bios_writer);
    }
    return ipxe_script_generate_static(
        furi_string_get_cstr(instance->ip_addr),
        furi_string_get_cstr(instance->subnet_mask),
        furi_string_get_cstr(instance->gateway),
        furi_string_get_cstr(instance->dns),
        furi_string_get_cstr(instance->chainload_url),
        furi_string_get_cstr(instance->network_interface),
        instance->chainload_enabled,
        bios_writer);
}

// The host wrote to .B2F, called from the USB thread
static void b2f_update_callback(void* context) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)context;
    if(b2f_dir_get_mode(instance->b2f) == B2fModePrompt) {
        // Views are switched on the GUI thread
        view_dispatcher_send_custom_event(((App*)instance->app)->view_dispatcher, EVENT_PROMPT);
        return;
    }
    view_get_model(instance->view);
    view_commit_model(instance->view, true);
}

// Answer the shown prompt (NULL cancels) and go back to the main view
static void usb_mass_storage_answer_prompt(AppUsbMassStorage* instance, const char* answer) {
    if(instance->prompt_view == 0) return;
    instance->prompt_view = 0;
    if(instance->b2f) b2f_dir_answer_prompt(instance->b2f, answer);
    view_dispatcher_switch_to_view(((App*)instance->app)->view_dispatcher, THIS_SCENE);
}

static void prompt_choice_callback(void* context, uint32_t index) {
    AppUsbMassStorage* instance = context;
    if(index < instance->prompt.option_count) {
        usb_mass_storage_answer_prompt(instance, instance->prompt.options[index]);
    }
}

static void prompt_confirm_callback(void* context, bool right) {
    AppUsbMassStorage* instance = context;
    usb_mass_storage_answer_prompt(instance, instance->prompt.options[right ? 0 : 1]);
}

static void prompt_text_callback(void* context) {
    AppUsbMassStorage* instance = context;
    usb_mass_storage_answer_prompt(instance, instance->prompt_text_buffer);
}

static void prompt_ip_callback(void* context) {
    AppUsbMassStorage* instance = context;
    const uint8_t* ip = instance->prompt_ip_buffer;
    char text[16];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    usb_mass_storage_answer_prompt(instance, text);
}

static void prompt_number_callback(void* context, int32_t number) {
    AppUsbMassStorage* instance = context;
    char text[16];
    snprintf(text, sizeof(text), "%ld", number);
    usb_mass_storage_answer_prompt(instance, text);
}

// Show the prompt the host asked, unless it is already shown
static void usb_mass_storage_show_prompt(AppUsbMassStorage* instance) {
    App* app = instance->app;
    if(instance->state != UsbMassStorageStateActive || instance->b2f == NULL) return;

    uint32_t shown_id = instance->prompt.id;
    if(!b2f_dir_get_prompt(instance->b2f, &instance->prompt)) return;
    if(instance->prompt_view != 0 && instance->prompt.id == shown_id) return;

    B2fPrompt* prompt = &instance->prompt;
    const char* default_value = prompt->option_count > 0 ? prompt->options[0] : "";

    // Lists and keyboards have a one line header
    size_t header_length = strcspn(prompt->question, "\n");
    if(header_length >= sizeof(instance->prompt_header)) {
        header_length = sizeof(instance->prompt_header) - 1;
    }
    memcpy(instance->prompt_header, prompt->question, header_length);
    instance->prompt_header[header_length] = '\0';
    const char* header = instance->prompt_header;

    if(prompt->type == B2fPromptConfirm) {
        confirm_input_set(
            instance->prompt_confirm,
            prompt->question,
            prompt->option_count > 1 ? prompt->options[1] : NULL,
            prompt->options[0]);
        confirm_input_set_callback(instance->prompt_confirm, prompt_confirm_callback, instance);
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM;
    } else if(prompt->type == B2fPromptChoice) {
        submenu_reset(instance->prompt_choice);
        submenu_set_header(instance->prompt_choice, header);
        for(uint8_t i = 0; i < prompt->option_count; i++) {
            submenu_add_item(
                instance->prompt_choice, prompt->options[i], i, prompt_choice_callback, instance);
        }
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_CHOICE;
    } else if(prompt->keyboard == B2fKeyboardIp) {
        unsigned int octets[4];
        bool prefill =
            sscanf(default_value, "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2], &octets[3]) ==
                4 &&
            octets[0] <= 255 && octets[1] <= 255 && octets[2] <= 255 && octets[3] <= 255;
        for(uint8_t i = 0; i < 4; i++) {
            instance->prompt_ip_buffer[i] = prefill ? octets[i] : 0;
        }
        ip_input_set_header_text(instance->prompt_ip, header);
        ip_input_set_result_callback(
            instance->prompt_ip, prompt_ip_callback, instance, instance->prompt_ip_buffer, prefill);
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_IP;
    } else if(prompt->keyboard == B2fKeyboardNumber) {
        number_input_set_header_text(instance->prompt_number, header);
        number_input_set_result_callback(
            instance->prompt_number,
            prompt_number_callback,
            instance,
            atoi(default_value),
            -999999999,
            999999999);
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_NUMBER;
    } else {
        strlcpy(instance->prompt_text_buffer, default_value, sizeof(instance->prompt_text_buffer));
        url_input_set_header_text(instance->prompt_text, header);
        url_input_set_result_callback(
            instance->prompt_text,
            prompt_text_callback,
            instance,
            instance->prompt_text_buffer,
            sizeof(instance->prompt_text_buffer));
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_TEXT;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, instance->prompt_view);
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
            view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_START);
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
    instance->b2f = NULL;
    instance->scsi = NULL;
    instance->msc = NULL;

    instance->prompt_choice = submenu_alloc();
    instance->prompt_text = url_input_alloc();
    instance->prompt_ip = ip_input_alloc();
    instance->prompt_number = number_input_alloc();
    instance->prompt_confirm = confirm_input_alloc();
    instance->prompt_views_added = false;
    instance->prompt_view = 0;
    memset(&instance->prompt, 0, sizeof(instance->prompt));

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

    b2f_dir_free(instance->b2f);

    // The prompt views are removed before they are freed
    if(instance->prompt_views_added) {
        ViewDispatcher* view_dispatcher = ((App*)instance->app)->view_dispatcher;
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_CHOICE);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_TEXT);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_IP);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_NUMBER);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM);
    }
    submenu_free(instance->prompt_choice);
    url_input_free(instance->prompt_text);
    ip_input_free(instance->prompt_ip);
    number_input_free(instance->prompt_number);
    confirm_input_free(instance->prompt_confirm);

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
    B2F_LOG_I("UsbMassStorage", "UsbMassStorage_get_view called, instance: %p", instance);
    if(instance == NULL) {
        B2F_LOG_E("UsbMassStorage", "instance is NULL in get_view!");
        return NULL;
    }
    B2F_LOG_I("UsbMassStorage", "instance->view: %p", instance->view);
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
    instance->prompt_view = 0;
    furi_string_reset(instance->current_file);

    if(!instance->prompt_views_added) {
        view_dispatcher_add_view(
            app->view_dispatcher,
            USB_MASS_STORAGE_VIEW_PROMPT_CHOICE,
            submenu_get_view(instance->prompt_choice));
        view_dispatcher_add_view(
            app->view_dispatcher,
            USB_MASS_STORAGE_VIEW_PROMPT_TEXT,
            url_input_get_view(instance->prompt_text));
        view_dispatcher_add_view(
            app->view_dispatcher,
            USB_MASS_STORAGE_VIEW_PROMPT_IP,
            ip_input_get_view(instance->prompt_ip));
        view_dispatcher_add_view(
            app->view_dispatcher,
            USB_MASS_STORAGE_VIEW_PROMPT_NUMBER,
            number_input_get_view(instance->prompt_number));
        view_dispatcher_add_view(
            app->view_dispatcher,
            USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM,
            confirm_input_get_view(instance->prompt_confirm));
        instance->prompt_views_added = true;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool UsbMassStorage_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    AppUsbMassStorage* instance = app->allocated_scenes[THIS_SCENE];

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == EVENT_PROMPT) {
            usb_mass_storage_show_prompt(instance);
            return true;
        } else if(event.event == EVENT_FILE_READ) {
            // Just trigger view redraw
            view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
            return true;
        } else if(event.event == EVENT_START) { // OK button pressed
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
            FuriString* ipxe_script = usb_mass_storage_generate_script(instance, true);

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

            // .B2F goes first, the other files are placed after its reserved area
            b2f_dir_free(instance->b2f);
            instance->b2f = b2f_dir_alloc();
            b2f_dir_set_update_callback(instance->b2f, b2f_update_callback, instance);
            if(!b2f_dir_add_to(instance->b2f, instance->vfat)) {
                furi_string_set(instance->status_text, "Failed to add .B2F");
                furi_string_free(ipxe_script);
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

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

            // BIOS iPXE runs BOOT.CFG as its command line, with the .B2F writer as
            // initrd. A script too long for the command line is passed as initrd
            // instead, without the writer.
            bool bios_cmdline = furi_string_size(ipxe_script) <= BIOS_BOOT_CMDLINE_MAX;
            if(!bios_cmdline) {
                furi_string_free(ipxe_script);
                ipxe_script = usb_mass_storage_generate_script(instance, false);
                ipxe_script_cstr = furi_string_get_cstr(ipxe_script);
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

            // Legacy BIOS boot loads IPXE.LKR with the script and the .B2F writer
            if(!virtual_fat_set_bios_boot_files(
                   instance->vfat,
                   "IPXE.LKR",
                   bios_cmdline ? B2F_PXE_PATH : "BOOT.CFG",
                   bios_cmdline ? "BOOT.CFG" : NULL)) {
                furi_string_set(instance->status_text, "Failed to set up BIOS boot");
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
        // Back on a prompt cancels it, USB keeps running
        if(instance->prompt_view != 0) {
            usb_mass_storage_answer_prompt(instance, NULL);
            return true;
        }

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

            b2f_dir_free(instance->b2f);
            instance->b2f = NULL;

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
