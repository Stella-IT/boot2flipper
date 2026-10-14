#include <gui/view.h>
#include <gui/modules/submenu.h>
#include <dialogs/dialogs.h>
#include "../../main.h"
#include "../../config/config.h"
#include "main.h"
#include "../../log.h"

#define THIS_SCENE Profiles
#define TAG        "Profiles"

AppProfiles* Profiles_alloc() {
    AppProfiles* profiles = malloc(sizeof(AppProfiles));
    profiles->submenu = submenu_alloc();
    profiles->header = furi_string_alloc();
    return profiles;
}

void Profiles_free(void* ptr) {
    AppProfiles* profiles = ptr;
    if(profiles == NULL) return;
    submenu_free(profiles->submenu);
    furi_string_free(profiles->header);
    free(profiles);
}

View* Profiles_get_view(void* ptr) {
    AppProfiles* profiles = ptr;
    return profiles ? submenu_get_view(profiles->submenu) : NULL;
}

// Pick a profile file, returns false if none was picked
static bool Profiles_browse(App* app, FuriString* path) {
    furi_string_set(path, app->config_file);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, CONFIG_FILE_EXTENSION, NULL);
    options.base_path = CONFIG_DIR_PATH;
    return dialog_file_browser_show(app->dialogs, path, path, &options);
}

static void Profiles_load(App* app) {
    FuriString* path = furi_string_alloc();
    if(Profiles_browse(app, path)) {
        // The profile being left keeps its changes
        app_save_config(app);

        Boot2FlipperConfig* loaded = config_alloc();
        if(config_load(app->storage, loaded, furi_string_get_cstr(path))) {
            config_copy(app->config, loaded);
            furi_string_set(app->config_file, path);
            config_set_last(app->storage, furi_string_get_cstr(path));
        } else {
            dialog_message_show_storage_error(app->dialogs, "Can't load profile");
        }
        config_free(loaded);
    }
    furi_string_free(path);
    scene_manager_previous_scene(app->scene_manager);
}

static void Profiles_delete(App* app) {
    FuriString* path = furi_string_alloc();
    if(Profiles_browse(app, path)) {
        FuriString* name = furi_string_alloc();
        config_get_profile_name(furi_string_get_cstr(path), name);

        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, "Delete profile?", 64, 0, AlignCenter, AlignTop);
        dialog_message_set_text(
            message, furi_string_get_cstr(name), 64, 30, AlignCenter, AlignCenter);
        dialog_message_set_buttons(message, "Cancel", NULL, "Delete");
        if(dialog_message_show(app->dialogs, message) == DialogMessageButtonRight &&
           storage_simply_remove(app->storage, furi_string_get_cstr(path)) &&
           furi_string_equal(path, app->config_file)) {
            // The settings stay, saved to the default profile from now on
            furi_string_set_str(app->config_file, CONFIG_FILE_PATH);
            config_set_last(app->storage, CONFIG_FILE_PATH);
        }
        dialog_message_free(message);
        furi_string_free(name);
    }
    furi_string_free(path);
    scene_manager_previous_scene(app->scene_manager);
}

static void Profiles_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case ProfilesItemLoad:
        Profiles_load(app);
        break;
    case ProfilesItemSaveAs:
        app->edit_target = EditTargetProfileName;
        scene_manager_next_scene(app->scene_manager, NameEdit);
        break;
    case ProfilesItemDelete:
        Profiles_delete(app);
        break;
    default:
        break;
    }
}

void Profiles_on_enter(void* context) {
    App* app = context;
    AppProfiles* profiles = app->allocated_scenes[THIS_SCENE];

    FuriString* name = furi_string_alloc();
    config_get_profile_name(furi_string_get_cstr(app->config_file), name);
    furi_string_printf(profiles->header, "Profile: %s", furi_string_get_cstr(name));
    furi_string_free(name);

    submenu_reset(profiles->submenu);
    submenu_set_header(profiles->submenu, furi_string_get_cstr(profiles->header));
    submenu_add_item(profiles->submenu, "Load Profile", ProfilesItemLoad, Profiles_callback, app);
    submenu_add_item(
        profiles->submenu, "Save as New Profile", ProfilesItemSaveAs, Profiles_callback, app);
    submenu_add_item(
        profiles->submenu, "Delete Profile", ProfilesItemDelete, Profiles_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool Profiles_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void Profiles_on_exit(void* context) {
    UNUSED(context);
}
