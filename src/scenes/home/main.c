#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/file_browser.h>
#include "../../main.h"
#include "../../config/config.h"
#include "../usb_mass_storage/usb_mass_storage.h"
#include "main.h"

#define THIS_SCENE Home
#define TAG        "Home"

static const char* network_mode_names[] = {"DHCP", "Static"};
static const char* partition_scheme_names[] = {"MBR", "UEFI"};
static const char* chainload_enabled_names[] = {"Disabled", "Enabled"};

// Forward declarations
static void Home_network_mode_change(VariableItem* item);
static void Home_partition_scheme_change(VariableItem* item);
static void Home_chainload_enabled_change(VariableItem* item);
static void Home_enter_callback(void* context, uint32_t index);
static void Home_build_menu(App* app);
static void Home_text_input_callback(void* context);
static void Home_file_browser_callback(void* context);

AppHome* Home_alloc() {
    AppHome* home = (AppHome*)malloc(sizeof(AppHome));
    home->var_item_list = variable_item_list_alloc();
    home->text_input = text_input_alloc();

    home->browser_result = furi_string_alloc();
    home->file_browser = file_browser_alloc(home->browser_result);

    home->network_item = NULL;
    home->network_settings_item = NULL;
    home->network_interface_item = NULL;
    home->partition_scheme_item = NULL;
    home->chainload_enabled_item = NULL;
    home->chainload_url_item = NULL;

    home->current_view = HOME_VIEW_MAIN_LIST;
    home->is_save_mode = false;
    home->is_network_interface_mode = false;

    return home;
}

void Home_free(void* ptr) {
    AppHome* home = (AppHome*)ptr;
    if(home == NULL) return;

    if(home->file_browser != NULL) {
        file_browser_free(home->file_browser);
    }

    if(home->browser_result != NULL) {
        furi_string_free(home->browser_result);
    }

    if(home->text_input != NULL) {
        text_input_free(home->text_input);
    }

    if(home->var_item_list != NULL) {
        variable_item_list_free(home->var_item_list);
    }

    free(home);
}

View* Home_get_view(void* ptr) {
    AppHome* home = (AppHome*)ptr;
    if(!home) return NULL;

    switch(home->current_view) {
    case HOME_VIEW_MAIN_LIST:
        return variable_item_list_get_view(home->var_item_list);
    case HOME_VIEW_TEXT_INPUT:
        return text_input_get_view(home->text_input);
    case HOME_VIEW_FILE_BROWSER:
        return file_browser_get_view(home->file_browser);
    default:
        return variable_item_list_get_view(home->var_item_list);
    }
}

static void Home_build_menu(App* app) {
    if(!app || !app->config) return;

    AppHome* home = app->allocated_scenes[THIS_SCENE];
    if(!home) return;

    variable_item_list_reset(home->var_item_list);

    // Load Config (index 0)
    variable_item_list_add(home->var_item_list, "Load Config", 0, NULL, NULL);

    // Save Config (index 1)
    variable_item_list_add(home->var_item_list, "Save Config", 0, NULL, NULL);

    // Network mode selector (index 2)
    home->network_item =
        variable_item_list_add(home->var_item_list, "Network", 2, Home_network_mode_change, app);
    uint8_t network_mode = app->config->dhcp ? 0 : 1;
    variable_item_set_current_value_index(home->network_item, network_mode);
    variable_item_set_current_value_text(home->network_item, network_mode_names[network_mode]);

    home->network_settings_item = variable_item_list_add(
        home->var_item_list,
        network_mode ? "Network Settings" : "Network Settings (Disabled)",
        0,
        NULL,
        NULL);

    // Network Interface (index 4)
    home->network_interface_item =
        variable_item_list_add(home->var_item_list, "Net Interface", 0, NULL, NULL);
    variable_item_set_current_value_text(
        home->network_interface_item, furi_string_get_cstr(app->config->network_interface));

    // Partition scheme selector (index 5)
    home->partition_scheme_item = variable_item_list_add(
        home->var_item_list, "Boot Method", 2, Home_partition_scheme_change, app);
    uint8_t partition_scheme = (uint8_t)app->config->partition_scheme;
    variable_item_set_current_value_index(home->partition_scheme_item, partition_scheme);
    variable_item_set_current_value_text(
        home->partition_scheme_item, partition_scheme_names[partition_scheme]);

    // Chainload enabled selector (index 6)
    home->chainload_enabled_item = variable_item_list_add(
        home->var_item_list, "Chainload", 2, Home_chainload_enabled_change, app);
    uint8_t chainload_enabled = app->config->chainload_enabled ? 1 : 0;
    variable_item_set_current_value_index(home->chainload_enabled_item, chainload_enabled);
    variable_item_set_current_value_text(
        home->chainload_enabled_item, chainload_enabled_names[chainload_enabled]);

    // Chainload URL (index 7) - show as disabled if chainload is disabled
    home->chainload_url_item =
        variable_item_list_add(home->var_item_list, "Chainload URL", 0, NULL, NULL);
    variable_item_set_current_value_text(
        home->chainload_url_item,
        app->config->chainload_enabled ? furi_string_get_cstr(app->config->chainload_url) :
                                         "Disabled");

    // Start
    variable_item_list_add(home->var_item_list, "Start", 0, NULL, NULL);

    variable_item_list_set_enter_callback(home->var_item_list, Home_enter_callback, app);
}

static void Home_text_input_callback(void* context) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    if(home->is_save_mode) {
        FuriString* save_path =
            furi_string_alloc_printf("/ext/apps_data/boot2flipper/%s.b2f", home->text_buffer);
        config_save(app->storage, app->config, furi_string_get_cstr(save_path));
        furi_string_free(save_path);
    } else if(home->is_network_interface_mode) {
        furi_string_set_str(app->config->network_interface, home->text_buffer);
    } else {
        furi_string_set_str(app->config->chainload_url, home->text_buffer);
    }

    Home_build_menu(app);
    home->current_view = HOME_VIEW_MAIN_LIST;
    view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_MAIN_LIST);
}

static void Home_file_browser_callback(void* context) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    FURI_LOG_I(TAG, "File selected: %s", furi_string_get_cstr(home->browser_result));

    config_load(app->storage, app->config, furi_string_get_cstr(home->browser_result));
    furi_string_set(app->config_file, furi_string_get_cstr(home->browser_result));

    Home_build_menu(app);
    home->current_view = HOME_VIEW_MAIN_LIST;
    view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_MAIN_LIST);
}

static void Home_network_mode_change(VariableItem* item) {
    App* app = variable_item_get_context(item);

    uint8_t index = variable_item_get_current_value_index(item);
    app->config->dhcp = (index == 0);

    variable_item_set_current_value_text(item, network_mode_names[index]);

    Home_build_menu(app);
}

static void Home_partition_scheme_change(VariableItem* item) {
    App* app = variable_item_get_context(item);

    uint8_t index = variable_item_get_current_value_index(item);
    app->config->partition_scheme = (PartitionScheme)index;

    variable_item_set_current_value_text(item, partition_scheme_names[index]);
}

static void Home_chainload_enabled_change(VariableItem* item) {
    App* app = variable_item_get_context(item);

    uint8_t index = variable_item_get_current_value_index(item);
    app->config->chainload_enabled = (index == 1);

    variable_item_set_current_value_text(item, chainload_enabled_names[index]);

    Home_build_menu(app);
}

static void Home_enter_callback(void* context, uint32_t index) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    switch(index) {
    case HOME_MENU_ITEM_LOAD:
        furi_string_set(home->browser_result, "/ext/apps_data/boot2flipper");
        file_browser_configure(
            home->file_browser, ".b2f", "/ext/apps_data/boot2flipper", true, true, NULL, false);
        file_browser_set_callback(home->file_browser, Home_file_browser_callback, app);
        file_browser_start(home->file_browser, home->browser_result);

        home->current_view = HOME_VIEW_FILE_BROWSER;
        view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_FILE_BROWSER);
        break;

    case HOME_MENU_ITEM_SAVE:
        memset(home->text_buffer, 0, sizeof(home->text_buffer));

        // Default filename
        strncpy(home->text_buffer, "config", sizeof(home->text_buffer) - 1);

        if(app->config_file != NULL) {
            // Extract filename without path and extension
            const char* full_path = furi_string_get_cstr(app->config_file);
            const char* last_slash = strrchr(full_path, '/');
            const char* filename_start = last_slash ? last_slash + 1 : full_path;
            const char* dot = strrchr(filename_start, '.');
            size_t name_length = dot ? (size_t)(dot - filename_start) : strlen(filename_start);
            if(name_length < sizeof(home->text_buffer)) {
                strncpy(home->text_buffer, filename_start, name_length);
                home->text_buffer[name_length] = '\0';
            } else {
                strncpy(home->text_buffer, filename_start, sizeof(home->text_buffer) - 1);
                home->text_buffer[sizeof(home->text_buffer) - 1] = '\0';
            }
        }

        text_input_set_header_text(home->text_input, "Enter filename");
        text_input_set_result_callback(
            home->text_input,
            Home_text_input_callback,
            app,
            home->text_buffer,
            sizeof(home->text_buffer),
            true);

        home->is_save_mode = true;
        home->current_view = HOME_VIEW_TEXT_INPUT;
        view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_TEXT_INPUT);
        break;

    case HOME_MENU_ITEM_NETWORK:
        // Handled by change callback
        break;

    case HOME_MENU_ITEM_NETWORK_SETTINGS:
        if(!app->config->dhcp) {
            scene_manager_next_scene(app->scene_manager, NetworkSettings);
        }
        // If DHCP mode, do nothing (menu item already shows "Disabled")
        break;

    case HOME_MENU_ITEM_NETWORK_INTERFACE:
        strncpy(
            home->text_buffer,
            furi_string_get_cstr(app->config->network_interface),
            sizeof(home->text_buffer) - 1);
        home->text_buffer[sizeof(home->text_buffer) - 1] = '\0';

        text_input_set_header_text(home->text_input, "Network Interface (e.g. net0)");
        text_input_set_result_callback(
            home->text_input,
            Home_text_input_callback,
            app,
            home->text_buffer,
            sizeof(home->text_buffer),
            true);

        home->is_save_mode = false;
        home->is_network_interface_mode = true;
        home->current_view = HOME_VIEW_TEXT_INPUT;
        view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_TEXT_INPUT);
        break;

    case HOME_MENU_ITEM_PARTITION_SCHEME:
        // Handled by change callback
        break;

    case HOME_MENU_ITEM_CHAINLOAD_ENABLED:
        // Handled by change callback
        break;

    case HOME_MENU_ITEM_CHAINLOAD_URL:
        if(!app->config->chainload_enabled) {
            // If chainload is disabled, do nothing (menu item already shows "Disabled")
            break;
        }
        strncpy(
            home->text_buffer,
            furi_string_get_cstr(app->config->chainload_url),
            sizeof(home->text_buffer) - 1);
        home->text_buffer[sizeof(home->text_buffer) - 1] = '\0';

        text_input_set_header_text(home->text_input, "Enter Chainload URL");
        text_input_set_result_callback(
            home->text_input,
            Home_text_input_callback,
            app,
            home->text_buffer,
            sizeof(home->text_buffer),
            true);

        home->is_save_mode = false;
        home->is_network_interface_mode = false;
        home->current_view = HOME_VIEW_TEXT_INPUT;
        view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_TEXT_INPUT);
        break;

    case HOME_MENU_ITEM_START: {
        AppUsbMassStorage* usb_instance = app->allocated_scenes[UsbMassStorage];

        UsbMassStorage_set_config(
            usb_instance,
            app->config->dhcp,
            furi_string_get_cstr(app->config->ip_addr),
            furi_string_get_cstr(app->config->subnet_mask),
            furi_string_get_cstr(app->config->gateway),
            furi_string_get_cstr(app->config->dns),
            furi_string_get_cstr(app->config->chainload_url),
            furi_string_get_cstr(app->config->network_interface),
            app->config->partition_scheme,
            app->config->chainload_enabled);

        scene_manager_next_scene(app->scene_manager, UsbMassStorage);
        break;
    }

    default:
        break;
    }
}

void Home_on_enter(void* context) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    // Register views only if not already registered (first time entering Home)
    static bool views_registered = false;
    if(!views_registered) {
        view_dispatcher_add_view(
            app->view_dispatcher,
            HOME_VIEW_MAIN_LIST,
            variable_item_list_get_view(home->var_item_list));
        view_dispatcher_add_view(
            app->view_dispatcher, HOME_VIEW_TEXT_INPUT, text_input_get_view(home->text_input));
        view_dispatcher_add_view(
            app->view_dispatcher,
            HOME_VIEW_FILE_BROWSER,
            file_browser_get_view(home->file_browser));
        views_registered = true;
    }

    Home_build_menu(app);

    home->current_view = HOME_VIEW_MAIN_LIST;
    view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_MAIN_LIST);
}

bool Home_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    AppHome* home = app->allocated_scenes[THIS_SCENE];

    if(event.type == SceneManagerEventTypeBack) {
        // If we're in a sub-view (text input or file browser), return to main list
        if(home->current_view == HOME_VIEW_TEXT_INPUT ||
           home->current_view == HOME_VIEW_FILE_BROWSER) {
            home->current_view = HOME_VIEW_MAIN_LIST;
            view_dispatcher_switch_to_view(app->view_dispatcher, HOME_VIEW_MAIN_LIST);
            return true; // Consume the back event
        }
        // If we're in main list, allow exit
        return false;
    }

    return true;
}

void Home_on_exit(void* context) {
    App* app = (App*)context;
    if(app == NULL || app->allocated_scenes == NULL) {
        return;
    }

    AppHome* home = app->allocated_scenes[THIS_SCENE];
    if(home == NULL) {
        return;
    }

    // Don't reset or stop views during on_exit - they may still be active
    // The scene manager hasn't switched views yet, so manipulating them causes furi_check
    // Views will be properly cleaned up when the scene is re-entered or freed
}
