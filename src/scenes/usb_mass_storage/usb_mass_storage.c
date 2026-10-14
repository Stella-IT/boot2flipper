#include <gui/elements.h>
#include "../../main.h"
#include "usb_mass_storage.h"
#include "../../ipxe/script_generator.h"
#include "../../ipxe/ipxe_validator.h"
#include "../../disk/virtual_fat.h"
#include "../../disk/bios_boot.h"
#include "../../usb/usb_scsi.h"
#include "../../usb/usb_msc.h"
#include "../../log.h"
#include "../../views/key_icons.h"
#include "../../views/tab_button.h"

#define THIS_SCENE UsbMassStorage

#define EVENT_FILE_READ 0xFF
#define EVENT_START     0x01
#define EVENT_PROMPT    0x10 // The host asked a prompt

#define STATUS_LINES       3
#define STATUS_LINE_HEIGHT 10

// Status text set by the host through .B2F/status, one line per '\n',
// lines too long for the screen scroll
static void usb_mass_storage_draw_status(Canvas* canvas, const char* text, size_t scroll) {
    FuriString* line_text = furi_string_alloc();
    uint8_t y = 24;
    for(uint8_t line = 0; line < STATUS_LINES && *text != '\0'; line++) {
        const char* end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        furi_string_set_strn(line_text, text, length);
        elements_scrollable_text_line(canvas, 10, y, 116, line_text, scroll, false);

        y += STATUS_LINE_HEIGHT;
        text = end ? end + 1 : text + strlen(text);
    }
    furi_string_free(line_text);
}

// Value of "key=value" in the host's system information, empty if missing
static void usb_mass_storage_sysinfo_value(const char* sysinfo, const char* key, FuriString* out) {
    furi_string_reset(out);
    size_t key_length = strlen(key);
    for(const char* line = sysinfo; *line != '\0';) {
        const char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if(length > key_length && memcmp(line, key, key_length) == 0 && line[key_length] == '=') {
            furi_string_set_strn(out, line + key_length + 1, length - key_length - 1);
            return;
        }
        line = end ? end + 1 : line + length;
    }
}

#define SYSINFO_LINE_FIRST  20 // Baselines, clear of the buttons
#define SYSINFO_LINE_HEIGHT 9

// The host's system information: model, serial number, UUID and platform.
// Labels stay, values too long for the screen scroll.
static void usb_mass_storage_draw_sysinfo(Canvas* canvas, const char* sysinfo, size_t scroll) {
    canvas_draw_str(canvas, 2, 10, "System Info");
    canvas_set_font(canvas, FontSecondary);

    FuriString* value = furi_string_alloc();
    FuriString* line = furi_string_alloc();
    static const char* const rows[][3] = {
        {NULL, "manufacturer", "product"},
        {"S/N", "serial", NULL},
        {"UUID", "uuid", NULL},
        {NULL, "platform", "arch"},
    };
    for(uint8_t row = 0; row < COUNT_OF(rows); row++) {
        furi_string_reset(line);
        for(uint8_t key = 1; key < 3 && rows[row][key]; key++) {
            usb_mass_storage_sysinfo_value(sysinfo, rows[row][key], value);
            if(furi_string_empty(value)) continue;
            if(!furi_string_empty(line)) furi_string_push_back(line, ' ');
            furi_string_cat(line, value);
        }
        if(furi_string_empty(line)) furi_string_set_str(line, "-");

        int32_t y = SYSINFO_LINE_FIRST + row * SYSINFO_LINE_HEIGHT;
        int32_t x = 2;
        if(rows[row][0]) {
            canvas_draw_str(canvas, x, y, rows[row][0]);
            x += canvas_string_width(canvas, rows[row][0]) + 4;
        }
        elements_scrollable_text_line(
            canvas, x, y, canvas_width(canvas) - x - 1, line, scroll, false);
    }
    furi_string_free(line);
    furi_string_free(value);

    tab_button_draw(
        canvas, false, key_icon_back_bits, KEY_ICON_BACK_WIDTH, KEY_ICON_BACK_HEIGHT, "Status");
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

        char sysinfo[B2F_SYSINFO_SIZE] = "";
        if(instance->b2f) b2f_dir_get_sysinfo(instance->b2f, sysinfo, sizeof(sysinfo));
        if(instance->show_sysinfo && sysinfo[0] != '\0') {
            usb_mass_storage_draw_sysinfo(canvas, sysinfo, instance->status_scroll);
            break;
        }

        canvas_draw_str(canvas, 10, 12, "Boot2Flipper Ready");
        canvas_set_font(canvas, FontSecondary);

        // Status from the host replaces the file being read
        char status[STATUS_LINES * 64] = "";
        if(instance->b2f && b2f_dir_get_status(instance->b2f, status, sizeof(status)) > 0) {
            usb_mass_storage_draw_status(canvas, status, instance->status_scroll);
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
        tab_button_draw(
            canvas, false, key_icon_back_bits, KEY_ICON_BACK_WIDTH, KEY_ICON_BACK_HEIGHT, "Stop");
        if(sysinfo[0] != '\0') {
            tab_button_draw(
                canvas,
                true,
                key_icon_right_bits,
                KEY_ICON_RIGHT_WIDTH,
                KEY_ICON_RIGHT_HEIGHT,
                "System Info");
        }
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

// Longest SD card script for CHAINLOAD_TARGET_FILE, inlined on BIOS
#define CHAIN_FILE_MAX 8192

// Reads the CHAINLOAD_TARGET_FILE script for BIOS iPXE, which can't read the disk
static FuriString* usb_mass_storage_read_chain_file(Storage* storage, const char* path) {
    FuriString* content = NULL;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_size(file) <= CHAIN_FILE_MAX) {
        size_t size = storage_file_size(file);
        char* buffer = malloc(size + 1);
        if(storage_file_read(file, buffer, size) == size) {
            buffer[size] = '\0';
            content = furi_string_alloc_set_str(buffer);
        }
        free(buffer);
    }
    storage_file_free(file);
    return content;
}

static const char* const chainload_target_names[CHAINLOAD_TARGET_COUNT] =
    {"URL", "Script", "iPXE shell", "Flipper shell"};
static const char* const partition_scheme_names[PARTITION_SCHEME_COUNT] = {"MBR", "UEFI", "Hybrid"};

// The settings the boot starts with
static void usb_mass_storage_log_config(AppUsbMassStorage* instance) {
    const Boot2FlipperConfig* config = instance->config;
    App* app = instance->app;
    BootLog* log = instance->boot_log;
    if(log == NULL) return;

    boot_log_write(log, "Boot2Flipper, profile %s", furi_string_get_cstr(app->config_file));
    if(config->dhcp) {
        boot_log_write(
            log, "Network: DHCP on %s", furi_string_get_cstr(config->network_interface));
    } else {
        boot_log_write(
            log,
            "Network: %s/%s gateway %s DNS %s on %s",
            furi_string_get_cstr(config->ip_addr),
            furi_string_get_cstr(config->subnet_mask),
            furi_string_get_cstr(config->gateway),
            furi_string_get_cstr(config->dns),
            furi_string_get_cstr(config->network_interface));
    }
    boot_log_write(
        log,
        "Boot method: %s, then: %s %s %s",
        partition_scheme_names[config->partition_scheme],
        chainload_target_names[config->chainload_target],
        config->chainload_target == CHAINLOAD_TARGET_URL ?
            furi_string_get_cstr(config->chainload_url) :
        config->chainload_target == CHAINLOAD_TARGET_FILE ?
            furi_string_get_cstr(config->chainload_file) :
            "",
        config->chainload_target == CHAINLOAD_TARGET_URL ?
            furi_string_get_cstr(config->chainload_args) :
            "");
}

// Builds the disk's files after .B2F, returns an error message or NULL
static const char* usb_mass_storage_add_files(AppUsbMassStorage* instance, Storage* storage) {
    const Boot2FlipperConfig* config = instance->config;
    VirtualFat* vfat = instance->vfat;
    const char* chain_file = furi_string_get_cstr(config->chainload_file);
    bool chain_from_file = config->chainload_target == CHAINLOAD_TARGET_FILE;

    // UEFI iPXE runs AUTOEXEC.IPXE and chains CHAIN.IPXE from the disk
    FuriString* script = ipxe_script_generate(config, true, NULL);
    bool added = virtual_fat_add_text_file(vfat, "AUTOEXEC.IPXE", furi_string_get_cstr(script));
    furi_string_free(script);
    if(!added) return "Failed to add AUTOEXEC.IPXE";
    if(chain_from_file &&
       !virtual_fat_add_sd_file(storage, vfat, IPXE_SCRIPT_CHAIN_FILE, chain_file)) {
        return "Failed to add chain script";
    }

    // BIOS iPXE runs BOOT.CFG as its command line, with the .B2F writer as
    // initrd. A script too long for the command line is passed as initrd
    // instead, without the writer.
    FuriString* chain_script = NULL;
    if(chain_from_file) {
        chain_script = usb_mass_storage_read_chain_file(storage, chain_file);
        if(!chain_script) return "Chain script too large";
    }
    const char* local_script = chain_script ? furi_string_get_cstr(chain_script) : NULL;
    script = ipxe_script_generate(config, true, local_script);
    bool bios_cmdline = furi_string_size(script) <= BIOS_BOOT_CMDLINE_MAX;
    if(!bios_cmdline) {
        furi_string_free(script);
        script = ipxe_script_generate(config, false, local_script);
    }
    if(chain_script) furi_string_free(chain_script);
    added = virtual_fat_add_text_file(vfat, "BOOT.CFG", furi_string_get_cstr(script));
    furi_string_free(script);
    if(!added) return "Failed to add BOOT.CFG";

    if(!virtual_fat_add_sd_file(storage, vfat, "IPXE.LKR", IPXE_BIOS_PATH)) {
        return "Failed to add IPXE.LKR";
    }
    if(!virtual_fat_add_file_to_subdir(storage, vfat, "EFI/BOOT", "BOOTX64.EFI", IPXE_UEFI_PATH)) {
        return "Failed to add BOOTX64.EFI";
    }

    // Legacy BIOS boot loads IPXE.LKR with the script and the .B2F writer
    if(!virtual_fat_set_bios_boot_files(
           vfat,
           "IPXE.LKR",
           bios_cmdline ? B2F_PXE_PATH : "BOOT.CFG",
           bios_cmdline ? "BOOT.CFG" : NULL)) {
        return "Failed to set up BIOS boot";
    }
    return NULL;
}

// The host wrote to .B2F, called from the USB thread
// Log the status and system information when the host changes them
static void usb_mass_storage_log_changes(AppUsbMassStorage* instance) {
    if(instance->boot_log == NULL) return;
    b2f_dir_get_status(instance->b2f, instance->log_buffer, sizeof(instance->log_buffer));
    if(strcmp(instance->log_buffer, instance->logged_status) != 0) {
        strlcpy(instance->logged_status, instance->log_buffer, sizeof(instance->logged_status));
        boot_log_write(instance->boot_log, "Status: %s", instance->log_buffer);
    }
    b2f_dir_get_sysinfo(instance->b2f, instance->log_buffer, sizeof(instance->log_buffer));
    if(strcmp(instance->log_buffer, instance->logged_sysinfo) != 0) {
        strlcpy(instance->logged_sysinfo, instance->log_buffer, sizeof(instance->logged_sysinfo));
        // One line per key
        for(char* line = instance->log_buffer; *line != '\0';) {
            char* end = strchr(line, '\n');
            if(end) *end = '\0';
            boot_log_write(instance->boot_log, "System: %s", line);
            line = end ? end + 1 : line + strlen(line);
        }
    }
}

static void b2f_log_callback(void* context, const char* line) {
    AppUsbMassStorage* instance = context;
    boot_log_write(instance->boot_log, "Host: %s", line);
}

static void b2f_update_callback(void* context) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)context;
    usb_mass_storage_log_changes(instance);
    if(b2f_dir_get_mode(instance->b2f) == B2fModePrompt) {
        // Views are switched on the GUI thread
        view_dispatcher_send_custom_event(((App*)instance->app)->view_dispatcher, EVENT_PROMPT);
        return;
    }
    instance->status_scroll = 0; // New status scrolls from its start
    view_get_model(instance->view);
    view_commit_model(instance->view, true);
}

// Answer the shown prompt and go back to the main view
static void usb_mass_storage_answer_prompt(
    AppUsbMassStorage* instance,
    B2fAnswer result,
    const char* value) {
    if(instance->prompt_view == 0) return;
    instance->prompt_view = 0;
    boot_log_write(
        instance->boot_log,
        "Answer: %s",
        result == B2fAnswerOk      ? value :
        result == B2fAnswerTimeout ? "(timeout)" :
                                     "(cancel)");
    if(instance->b2f) b2f_dir_answer_prompt(instance->b2f, result, value);
    view_dispatcher_switch_to_view(((App*)instance->app)->view_dispatcher, THIS_SCENE);
}

// Show the prompt's header (with the countdown, if seconds >= 0) in its view
static void usb_mass_storage_show_countdown(AppUsbMassStorage* instance, int32_t seconds) {
    if(seconds >= 0) {
        snprintf(
            instance->prompt_header,
            sizeof(instance->prompt_header),
            "%s (%ld)",
            instance->prompt_title,
            seconds);
    } else {
        strlcpy(instance->prompt_header, instance->prompt_title, sizeof(instance->prompt_header));
    }

    switch(instance->prompt_view) {
    case USB_MASS_STORAGE_VIEW_PROMPT_CHOICE:
        submenu_set_header(instance->prompt_choice, instance->prompt_header);
        break;
    case USB_MASS_STORAGE_VIEW_PROMPT_TEXT:
        url_input_set_header_text(instance->prompt_text, instance->prompt_header);
        break;
    case USB_MASS_STORAGE_VIEW_PROMPT_IP:
        ip_input_set_header_text(instance->prompt_ip, instance->prompt_header);
        break;
    case USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM:
        confirm_input_set_countdown(instance->prompt_confirm, seconds);
        break;
    }
}

// The timeout only runs while nobody touches the Flipper
static void usb_mass_storage_prompt_touched(AppUsbMassStorage* instance) {
    if(instance->prompt_view == 0 || instance->prompt_touched) return;
    instance->prompt_touched = true;
    if(instance->prompt.timeout > 0) usb_mass_storage_show_countdown(instance, -1);
}

static void prompt_activity_callback(void* context) {
    usb_mass_storage_prompt_touched(context);
}

// Count the prompt's timeout down in its view, answer when it runs out
static void usb_mass_storage_update_countdown(AppUsbMassStorage* instance) {
    if(instance->prompt_view == 0 || instance->prompt.timeout == 0) return;

    // The stock list doesn't report keys, a moved selection does
    if(instance->prompt_view == USB_MASS_STORAGE_VIEW_PROMPT_CHOICE &&
       submenu_get_selected_item(instance->prompt_choice) != 0) {
        usb_mass_storage_prompt_touched(instance);
    }
    if(instance->prompt_touched) return;

    int32_t left = (int32_t)(instance->prompt_deadline - furi_get_tick());
    if(left <= 0) {
        usb_mass_storage_answer_prompt(instance, B2fAnswerTimeout, NULL);
        return;
    }
    uint32_t frequency = furi_kernel_get_tick_frequency();
    int32_t seconds = (left + frequency - 1) / frequency;
    if(seconds == instance->prompt_seconds) return;
    instance->prompt_seconds = seconds;
    usb_mass_storage_show_countdown(instance, seconds);
}

static void prompt_choice_callback(void* context, uint32_t index) {
    AppUsbMassStorage* instance = context;
    if(index < instance->prompt.option_count) {
        usb_mass_storage_answer_prompt(instance, B2fAnswerOk, instance->prompt.options[index]);
    }
}

static void prompt_confirm_callback(void* context, bool right) {
    AppUsbMassStorage* instance = context;
    usb_mass_storage_answer_prompt(instance, B2fAnswerOk, instance->prompt.options[right ? 0 : 1]);
}

static void prompt_text_callback(void* context) {
    AppUsbMassStorage* instance = context;
    usb_mass_storage_answer_prompt(instance, B2fAnswerOk, instance->prompt_text_buffer);
}

static void prompt_ip_callback(void* context) {
    AppUsbMassStorage* instance = context;
    const uint8_t* ip = instance->prompt_ip_buffer;
    char text[16];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    usb_mass_storage_answer_prompt(instance, B2fAnswerOk, text);
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

    // Lists and keyboards have a one line header, with the countdown if any
    size_t title_length = strcspn(prompt->question, "\n");
    if(title_length >= sizeof(instance->prompt_title)) {
        title_length = sizeof(instance->prompt_title) - 1;
    }
    memcpy(instance->prompt_title, prompt->question, title_length);
    instance->prompt_title[title_length] = '\0';
    strlcpy(instance->prompt_header, instance->prompt_title, sizeof(instance->prompt_header));
    const char* header = instance->prompt_header;
    instance->prompt_seconds = -1;
    instance->prompt_touched = false;
    instance->prompt_deadline =
        furi_get_tick() + prompt->timeout * furi_kernel_get_tick_frequency();
    confirm_input_set_countdown(instance->prompt_confirm, -1);

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
    } else {
        strlcpy(instance->prompt_text_buffer, default_value, sizeof(instance->prompt_text_buffer));
        url_input_set_header_text(instance->prompt_text, header);
        url_input_set_result_callback(
            instance->prompt_text,
            prompt_text_callback,
            instance,
            instance->prompt_text_buffer,
            sizeof(instance->prompt_text_buffer));
        // Numbers start on the digits
        if(prompt->keyboard == B2fKeyboardNumber) url_input_select_digits(instance->prompt_text);
        instance->prompt_view = USB_MASS_STORAGE_VIEW_PROMPT_TEXT;
    }

    if(instance->boot_log) {
        FuriString* options = furi_string_alloc();
        for(uint8_t i = 0; i < prompt->option_count; i++) {
            furi_string_cat_printf(options, " [%s]", prompt->options[i]);
        }
        boot_log_write(
            instance->boot_log,
            "Prompt: %s%s",
            instance->prompt_title,
            furi_string_get_cstr(options));
        furi_string_free(options);
    }

    usb_mass_storage_update_countdown(instance);
    view_dispatcher_switch_to_view(app->view_dispatcher, instance->prompt_view);
}

static bool usb_mass_storage_input_callback(InputEvent* event, void* context) {
    AppUsbMassStorage* instance = (AppUsbMassStorage*)context;

    if(!instance || !instance->app) {
        return false;
    }

    App* app = (App*)instance->app;

    // Right shows the host's system information, Left goes back
    if(event->type == InputTypeShort && instance->state == UsbMassStorageStateActive &&
       (event->key == InputKeyRight || event->key == InputKeyLeft)) {
        instance->show_sysinfo = event->key == InputKeyRight;
        instance->status_scroll = 0;
        view_get_model(instance->view);
        view_commit_model(instance->view, true);
        return true;
    }

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
    instance->boot_log = NULL;
    instance->show_sysinfo = false;
    instance->scsi = NULL;
    instance->msc = NULL;

    instance->prompt_choice = submenu_alloc();
    instance->prompt_text = url_input_alloc();
    instance->prompt_ip = ip_input_alloc();
    instance->prompt_confirm = confirm_input_alloc();
    url_input_set_activity_callback(instance->prompt_text, prompt_activity_callback, instance);
    ip_input_set_activity_callback(instance->prompt_ip, prompt_activity_callback, instance);
    confirm_input_set_activity_callback(
        instance->prompt_confirm, prompt_activity_callback, instance);
    instance->prompt_views_added = false;
    instance->prompt_view = 0;
    memset(&instance->prompt, 0, sizeof(instance->prompt));

    instance->config = config_alloc();
    instance->status_text = furi_string_alloc();
    instance->current_file = furi_string_alloc();

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
    boot_log_close(instance->boot_log);

    // The prompt views are removed before they are freed
    if(instance->prompt_views_added) {
        ViewDispatcher* view_dispatcher = ((App*)instance->app)->view_dispatcher;
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_CHOICE);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_TEXT);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_IP);
        view_dispatcher_remove_view(view_dispatcher, USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM);
    }
    submenu_free(instance->prompt_choice);
    url_input_free(instance->prompt_text);
    ip_input_free(instance->prompt_ip);
    confirm_input_free(instance->prompt_confirm);

    config_free(instance->config);
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

void UsbMassStorage_set_config(AppUsbMassStorage* instance, const Boot2FlipperConfig* config) {
    config_copy(instance->config, config);
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
            USB_MASS_STORAGE_VIEW_PROMPT_CONFIRM,
            confirm_input_get_view(instance->prompt_confirm));
        instance->prompt_views_added = true;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
    // Start pressed on the dashboard
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_START);
}

bool UsbMassStorage_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    AppUsbMassStorage* instance = app->allocated_scenes[THIS_SCENE];

    if(event.type == SceneManagerEventTypeTick) {
        usb_mass_storage_update_countdown(instance);
        if(instance->state == UsbMassStorageStateActive && instance->prompt_view == 0) {
            instance->status_scroll++;
            view_get_model(instance->view);
            view_commit_model(instance->view, true);
        }
        return false;
    }

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

            // 2. Create virtual FAT filesystem
            instance->vfat = virtual_fat_alloc();
            virtual_fat_set_partition_scheme(instance->vfat, instance->config->partition_scheme);
            virtual_fat_set_read_callback(instance->vfat, file_read_callback, instance);

            // 3. .B2F goes first, the other files are placed after its reserved area
            b2f_dir_free(instance->b2f);
            instance->b2f = b2f_dir_alloc();
            b2f_dir_set_update_callback(instance->b2f, b2f_update_callback, instance);
            const char* error = b2f_dir_add_to(instance->b2f, instance->vfat) ?
                                    usb_mass_storage_add_files(instance, storage) :
                                    "Failed to add .B2F";
            if(error) {
                furi_string_set(instance->status_text, error);
                virtual_fat_free(instance->vfat);
                instance->vfat = NULL;
                instance->state = UsbMassStorageStateError;
                furi_record_close(RECORD_STORAGE);
                view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
                return true;
            }

            // Log of the boot, the host's lines come through .B2F/log
            boot_log_close(instance->boot_log);
            instance->boot_log = NULL;
            instance->logged_status[0] = instance->logged_sysinfo[0] = '\0';
            instance->show_sysinfo = false;
            if(instance->config->write_log) {
                instance->boot_log = boot_log_open(storage);
                usb_mass_storage_log_config(instance);
                b2f_dir_set_log_callback(instance->b2f, b2f_log_callback, instance);
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
            usb_mass_storage_answer_prompt(instance, B2fAnswerCancel, NULL);
            return true;
        }

        // Back on the system information goes back to the status
        if(instance->show_sysinfo) {
            instance->show_sysinfo = false;
            view_get_model(instance->view);
            view_commit_model(instance->view, true);
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

            boot_log_write(instance->boot_log, "USB stopped");
            boot_log_close(instance->boot_log);
            instance->boot_log = NULL;

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
