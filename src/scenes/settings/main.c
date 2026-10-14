#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include <dialogs/dialogs.h>
#include "../../main.h"
#include "../../config/config.h"
#include "main.h"

#define THIS_SCENE Settings

#define EVENT_REBUILD 0x01

static const char* network_mode_names[] = {"DHCP", "Static"};
static const char* chainload_target_names[CHAINLOAD_TARGET_COUNT] =
    {"URL", "Script", "Shell", "F.shell"};
static const char* partition_scheme_names[PARTITION_SCHEME_COUNT] = {"MBR", "UEFI", "Hybrid"};

AppSettings* Settings_alloc() {
    AppSettings* settings = malloc(sizeof(AppSettings));
    settings->var_item_list = variable_item_list_alloc();
    settings->item_count = 0;
    return settings;
}

void Settings_free(void* ptr) {
    AppSettings* settings = ptr;
    if(settings == NULL) return;
    variable_item_list_free(settings->var_item_list);
    free(settings);
}

View* Settings_get_view(void* ptr) {
    AppSettings* settings = ptr;
    return settings ? variable_item_list_get_view(settings->var_item_list) : NULL;
}

// Rows are rebuilt from the event loop, not from inside the list's callbacks
static void Settings_rebuild_later(App* app) {
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_REBUILD);
}

static void Settings_network_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->dhcp = index == 0;
    variable_item_set_current_value_text(item, network_mode_names[index]);
    Settings_rebuild_later(app);
}

static void Settings_chainload_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->chainload_target = (ChainloadTarget)index;
    variable_item_set_current_value_text(item, chainload_target_names[index]);
    Settings_rebuild_later(app);
}

static void Settings_boot_method_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->partition_scheme = (PartitionScheme)index;
    variable_item_set_current_value_text(item, partition_scheme_names[index]);
}

static VariableItem* Settings_add(
    AppSettings* settings,
    SettingsItem id,
    const char* label,
    uint8_t values_count,
    VariableItemChangeCallback callback,
    App* app) {
    settings->items[settings->item_count++] = id;
    return variable_item_list_add(settings->var_item_list, label, values_count, callback, app);
}

static void Settings_add_choice(
    AppSettings* settings,
    SettingsItem id,
    const char* label,
    const char* const* names,
    uint8_t count,
    uint8_t index,
    VariableItemChangeCallback callback,
    App* app) {
    VariableItem* item = Settings_add(settings, id, label, count, callback, app);
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, names[index]);
}

static void Settings_build(App* app) {
    AppSettings* settings = app->allocated_scenes[THIS_SCENE];
    const Boot2FlipperConfig* config = app->config;
    uint8_t selected = variable_item_list_get_selected_item_index(settings->var_item_list);
    variable_item_list_reset(settings->var_item_list);
    settings->item_count = 0;

    FuriString* name = furi_string_alloc();
    config_get_profile_name(furi_string_get_cstr(app->config_file), name);
    VariableItem* item = Settings_add(settings, SettingsItemProfile, "Profile", 0, NULL, app);
    variable_item_set_current_value_text(item, furi_string_get_cstr(name));
    furi_string_free(name);

    Settings_add_choice(
        settings,
        SettingsItemNetwork,
        "Network",
        network_mode_names,
        2,
        config->dhcp ? 0 : 1,
        Settings_network_change,
        app);
    if(!config->dhcp) {
        item = Settings_add(settings, SettingsItemIpSettings, "IP Settings", 0, NULL, app);
        variable_item_set_current_value_text(item, furi_string_get_cstr(config->ip_addr));
    }

    Settings_add_choice(
        settings,
        SettingsItemChainload,
        "Then Run",
        chainload_target_names,
        CHAINLOAD_TARGET_COUNT,
        config->chainload_target,
        Settings_chainload_change,
        app);
    if(config->chainload_target == CHAINLOAD_TARGET_URL) {
        item = Settings_add(settings, SettingsItemUrl, "URL", 0, NULL, app);
        variable_item_set_current_value_text(item, furi_string_get_cstr(config->chainload_url));
        item = Settings_add(settings, SettingsItemArgs, "Arguments", 0, NULL, app);
        variable_item_set_current_value_text(
            item,
            furi_string_empty(config->chainload_args) ?
                "None" :
                furi_string_get_cstr(config->chainload_args));
    } else if(config->chainload_target == CHAINLOAD_TARGET_FILE) {
        const char* path = furi_string_get_cstr(config->chainload_file);
        const char* file = strrchr(path, '/');
        item = Settings_add(settings, SettingsItemScript, "Script File", 0, NULL, app);
        variable_item_set_current_value_text(item, path[0] ? (file ? file + 1 : path) : "Select");
    }

    Settings_add_choice(
        settings,
        SettingsItemBootMethod,
        "Boot Method",
        partition_scheme_names,
        PARTITION_SCHEME_COUNT,
        config->partition_scheme,
        Settings_boot_method_change,
        app);
    Settings_add(settings, SettingsItemAdvanced, "Advanced", 0, NULL, app);

    if(selected >= settings->item_count) selected = settings->item_count - 1;
    variable_item_list_set_selected_item(settings->var_item_list, selected);
}

// Pick the CHAINLOAD_TARGET_FILE script on the SD card
static void Settings_select_script(App* app) {
    FuriString* path = furi_string_alloc_set(app->config->chainload_file);
    if(furi_string_empty(path)) furi_string_set_str(path, CONFIG_DIR_PATH);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, "*", NULL);
    options.base_path = STORAGE_EXT_PATH_PREFIX;
    if(dialog_file_browser_show(app->dialogs, path, path, &options)) {
        furi_string_set(app->config->chainload_file, path);
    }
    furi_string_free(path);
    Settings_build(app);
}

static void Settings_enter_callback(void* context, uint32_t index) {
    App* app = context;
    AppSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(index >= settings->item_count) return;

    switch(settings->items[index]) {
    case SettingsItemProfile:
        scene_manager_next_scene(app->scene_manager, Profiles);
        break;
    case SettingsItemIpSettings:
        scene_manager_next_scene(app->scene_manager, NetworkSettings);
        break;
    case SettingsItemUrl:
        app->edit_target = EditTargetChainloadUrl;
        scene_manager_next_scene(app->scene_manager, TextEdit);
        break;
    case SettingsItemArgs:
        app->edit_target = EditTargetChainloadArgs;
        scene_manager_next_scene(app->scene_manager, TextEdit);
        break;
    case SettingsItemScript:
        Settings_select_script(app);
        break;
    case SettingsItemAdvanced:
        scene_manager_next_scene(app->scene_manager, Advanced);
        break;
    default:
        break;
    }
}

void Settings_on_enter(void* context) {
    App* app = context;
    AppSettings* settings = app->allocated_scenes[THIS_SCENE];
    variable_item_list_set_enter_callback(settings->var_item_list, Settings_enter_callback, app);
    Settings_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool Settings_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EVENT_REBUILD) {
        Settings_build(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        // Back to the dashboard: keep the changes in the profile
        app_save_config(app);
    }
    return false;
}

void Settings_on_exit(void* context) {
    UNUSED(context);
}
