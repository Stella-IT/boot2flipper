#include <gui/view.h>
#include <gui/elements.h>
#include "../../main.h"
#include "../../config/config.h"
#include "../../ipxe/ipxe_validator.h"
#include "../../views/key_icons.h"
#include "../../views/tab_button.h"
#include "../usb_mass_storage/usb_mass_storage.h"
#include "main.h"
#include "../../log.h"

#define THIS_SCENE Home
#define TAG        "Home"

#define EVENT_SETTINGS 0x01
#define EVENT_START    0x02

#define LINE_FIRST  23 // Baseline of the first setting line
#define LINE_HEIGHT 10
#define LABEL_X     2
#define VALUE_X     30

static const char* partition_scheme_names[PARTITION_SCHEME_COUNT] = {
    "MBR (BIOS)",
    "GPT (UEFI)",
    "Hybrid (BIOS+UEFI)"};

typedef struct {
    App* app;
    FuriString* profile;
    FuriString* network;
    FuriString* chainload;
    bool ipxe_missing;
    size_t scroll; // Marquee of lines too long for the screen
} HomeModel;

static void Home_draw_line(
    Canvas* canvas,
    uint8_t line,
    const char* label,
    FuriString* value,
    size_t scroll) {
    int32_t y = LINE_FIRST + line * LINE_HEIGHT;
    canvas_draw_str(canvas, LABEL_X, y, label);
    elements_scrollable_text_line(
        canvas, VALUE_X, y, canvas_width(canvas) - VALUE_X - 1, value, scroll, false);
}

static void Home_draw_callback(Canvas* canvas, void* _model) {
    HomeModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, APP_NAME);
    int32_t title_width = canvas_string_width(canvas, APP_NAME);

    // Profile name in the rest of the title line
    canvas_set_font(canvas, FontSecondary);
    elements_scrollable_text_line(
        canvas,
        title_width + 8,
        10,
        canvas_width(canvas) - title_width - 10,
        model->profile,
        model->scroll,
        true);
    canvas_draw_line(canvas, 0, 13, canvas_width(canvas) - 1, 13);

    Home_draw_line(canvas, 0, "Net", model->network, model->scroll);
    if(model->ipxe_missing) {
        canvas_draw_str(canvas, LABEL_X, LINE_FIRST + LINE_HEIGHT, "iPXE files missing on SD!");
    } else {
        FuriString* boot = furi_string_alloc_set_str(
            partition_scheme_names[model->app->config->partition_scheme]);
        Home_draw_line(canvas, 1, "Boot", boot, 0);
        furi_string_free(boot);
    }
    Home_draw_line(canvas, 2, "Then", model->chainload, model->scroll);

    tab_button_draw(
        canvas, false, key_icon_left_bits, KEY_ICON_LEFT_WIDTH, KEY_ICON_LEFT_HEIGHT, "Settings");
    tab_button_draw(
        canvas, true, key_icon_ok_bits, KEY_ICON_OK_WIDTH, KEY_ICON_OK_HEIGHT, "Start");
}

static bool Home_input_callback(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_SETTINGS);
        return true;
    }
    if(event->key == InputKeyOk) {
        view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_START);
        return true;
    }
    return false;
}

AppHome* Home_alloc() {
    AppHome* home = (AppHome*)malloc(sizeof(AppHome));
    home->view = view_alloc();
    view_allocate_model(home->view, ViewModelTypeLocking, sizeof(HomeModel));
    view_set_draw_callback(home->view, Home_draw_callback);
    view_set_input_callback(home->view, Home_input_callback);
    with_view_model(
        home->view,
        HomeModel * model,
        {
            model->app = NULL;
            model->profile = furi_string_alloc();
            model->network = furi_string_alloc();
            model->chainload = furi_string_alloc();
            model->ipxe_missing = false;
            model->scroll = 0;
        },
        false);
    return home;
}

void Home_free(void* ptr) {
    AppHome* home = (AppHome*)ptr;
    if(home == NULL) return;
    with_view_model(
        home->view,
        HomeModel * model,
        {
            furi_string_free(model->profile);
            furi_string_free(model->network);
            furi_string_free(model->chainload);
        },
        false);
    view_free(home->view);
    free(home);
}

View* Home_get_view(void* ptr) {
    AppHome* home = (AppHome*)ptr;
    return home ? home->view : NULL;
}

// Summarize the configuration for the view
static void Home_update(App* app) {
    AppHome* home = app->allocated_scenes[THIS_SCENE];
    const Boot2FlipperConfig* config = app->config;

    IpxeValidationResult validation;
    bool ipxe_missing = !ipxe_validate_binaries(app->storage, &validation);

    with_view_model(
        home->view,
        HomeModel * model,
        {
            model->app = app;
            model->ipxe_missing = ipxe_missing;
            model->scroll = 0;
            config_get_profile_name(furi_string_get_cstr(app->config_file), model->profile);

            const char* iface = furi_string_get_cstr(config->network_interface);
            furi_string_set_str(
                model->network, config->dhcp ? "DHCP" : furi_string_get_cstr(config->ip_addr));
            if(strcmp(iface, NETWORK_INTERFACE_AUTO) != 0) {
                furi_string_cat_printf(model->network, " on %s", iface);
            }

            switch(config->chainload_target) {
            case CHAINLOAD_TARGET_URL:
                furi_string_printf(
                    model->chainload,
                    "%s%s%s",
                    furi_string_get_cstr(config->chainload_url),
                    furi_string_empty(config->chainload_args) ? "" : " ",
                    furi_string_get_cstr(config->chainload_args));
                break;
            case CHAINLOAD_TARGET_FILE:
                if(furi_string_empty(config->chainload_file)) {
                    furi_string_set_str(model->chainload, "No script selected");
                } else {
                    const char* path = furi_string_get_cstr(config->chainload_file);
                    const char* name = strrchr(path, '/');
                    furi_string_printf(model->chainload, "Script %s", name ? name + 1 : path);
                }
                break;
            case CHAINLOAD_TARGET_FLIPPER_SHELL:
                furi_string_set_str(model->chainload, "iPXE commands from Flipper");
                break;
            default:
                furi_string_set_str(model->chainload, "iPXE shell");
                break;
            }
        },
        true);
}

void Home_on_enter(void* context) {
    App* app = (App*)context;
    Home_update(app);
    AppHome* home = app->allocated_scenes[THIS_SCENE];
    view_set_context(home->view, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool Home_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    if(event.type == SceneManagerEventTypeTick) {
        with_view_model(home->view, HomeModel * model, { model->scroll++; }, true);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == EVENT_SETTINGS) {
        scene_manager_next_scene(app->scene_manager, Settings);
        return true;
    }
    if(event.event == EVENT_START) {
        UsbMassStorage_set_config(app->allocated_scenes[UsbMassStorage], app->config);
        scene_manager_next_scene(app->scene_manager, UsbMassStorage);
        return true;
    }
    return false;
}

void Home_on_exit(void* context) {
    UNUSED(context);
}
