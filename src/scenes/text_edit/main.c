#include "../../main.h"
#include "main.h"

#define THIS_SCENE TextEdit

#define EVENT_SAVED 0x01

AppTextEdit* TextEdit_alloc() {
    AppTextEdit* edit = malloc(sizeof(AppTextEdit));
    edit->url_input = url_input_alloc();
    return edit;
}

void TextEdit_free(void* ptr) {
    AppTextEdit* edit = ptr;
    if(edit == NULL) return;
    url_input_free(edit->url_input);
    free(edit);
}

View* TextEdit_get_view(void* ptr) {
    AppTextEdit* edit = ptr;
    return edit ? url_input_get_view(edit->url_input) : NULL;
}

static FuriString* TextEdit_target(App* app) {
    return app->edit_target == EditTargetChainloadArgs ? app->config->chainload_args :
                                                         app->config->chainload_url;
}

static void TextEdit_callback(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, EVENT_SAVED);
}

void TextEdit_on_enter(void* context) {
    App* app = context;
    AppTextEdit* edit = app->allocated_scenes[THIS_SCENE];

    strlcpy(app->edit_buffer, furi_string_get_cstr(TextEdit_target(app)), APP_EDIT_BUFFER_SIZE);
    url_input_set_header_text(
        edit->url_input,
        app->edit_target == EditTargetChainloadArgs ? "Arguments (space: hold layer key)" :
                                                      "Chainload URL");
    url_input_set_result_callback(
        edit->url_input, TextEdit_callback, app, app->edit_buffer, APP_EDIT_BUFFER_SIZE);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool TextEdit_on_event(void* context, SceneManagerEvent event) {
    App* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == EVENT_SAVED) {
        furi_string_set_str(TextEdit_target(app), app->edit_buffer);
        furi_string_trim(TextEdit_target(app));
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void TextEdit_on_exit(void* context) {
    UNUSED(context);
}
