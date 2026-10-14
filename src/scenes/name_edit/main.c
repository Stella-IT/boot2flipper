#include "../../main.h"
#include "../../config/config.h"
#include "main.h"

#define THIS_SCENE NameEdit

#define EVENT_SAVED 0x01

// Names are short: profile file names and interface names
#define NAME_EDIT_SIZE 32

AppNameEdit* NameEdit_alloc() {
    AppNameEdit* edit = malloc(sizeof(AppNameEdit));
    edit->text_input = text_input_alloc();
    return edit;
}

void NameEdit_free(void* ptr) {
    AppNameEdit* edit = ptr;
    if(edit == NULL) return;
    text_input_free(edit->text_input);
    free(edit);
}

View* NameEdit_get_view(void* ptr) {
    AppNameEdit* edit = ptr;
    return edit ? text_input_get_view(edit->text_input) : NULL;
}

static void NameEdit_callback(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_SAVED);
}

void NameEdit_on_enter(void* context) {
    App* app = context;
    AppNameEdit* edit = app->allocated_scenes[THIS_SCENE];
    const char* header;

    if(app->edit_target == EditTargetProfileName) {
        FuriString* name = furi_string_alloc();
        config_get_profile_name(furi_string_get_cstr(app->config_file), name);
        strlcpy(app->edit_buffer, furi_string_get_cstr(name), NAME_EDIT_SIZE);
        furi_string_free(name);
        header = "New profile name";
    } else {
        strlcpy(
            app->edit_buffer,
            furi_string_get_cstr(app->config->network_interface),
            NAME_EDIT_SIZE);
        header = "Interface (e.g. net0)";
    }

    text_input_reset(edit->text_input);
    text_input_set_header_text(edit->text_input, header);
    text_input_set_minimum_length(edit->text_input, 1);
    text_input_set_result_callback(
        edit->text_input, NameEdit_callback, app, app->edit_buffer, NAME_EDIT_SIZE, true);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool NameEdit_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != EVENT_SAVED) return false;

    if(app->edit_target == EditTargetProfileName) {
        furi_string_printf(
            app->config_file, "%s/%s%s", CONFIG_DIR_PATH, app->edit_buffer, CONFIG_FILE_EXTENSION);
        app_save_config(app);
        // Back to the settings, past the profile menu
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, Settings);
    } else {
        furi_string_set_str(app->config->network_interface, app->edit_buffer);
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void NameEdit_on_exit(void* context) {
    UNUSED(context);
}
