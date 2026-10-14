#include <stdio.h>
#include <furi.h>
#include <gui/gui.h>
#include "main.h"
#include "scenes/register.h"
#include "log.h"

void app_save_config(App* app) {
    if(config_save(app->storage, app->config, furi_string_get_cstr(app->config_file))) {
        config_set_last(app->storage, furi_string_get_cstr(app->config_file));
    }
}

int main() {
    App* app = malloc(sizeof(App));
    furi_assert(app != NULL, "Failed to allocate memory for the app");

    // Initialize global resources
    app->gui = furi_record_open(RECORD_GUI);
    furi_assert(app->gui != NULL, "Failed to open the GUI record");

    app->storage = furi_record_open(RECORD_STORAGE);
    furi_assert(app->storage != NULL, "Failed to open the Storage record");

    app->dialogs = furi_record_open(RECORD_DIALOGS);
    furi_assert(app->dialogs != NULL, "Failed to open the Dialogs record");

    // Initialize global configuration
    app->config = config_alloc();

    // Start with the profile used last
    app->config_file = furi_string_alloc();
    config_load_last(app->storage, app->config, app->config_file);

    register_scenes(app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // The default scene is always the first one!
    scene_manager_next_scene(app->scene_manager, 0);

    view_dispatcher_run(app->view_dispatcher);

    B2F_LOG_I(APP_NAME, "Exiting application.");
    free_scenes(app);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    app->storage = NULL;
    app->dialogs = NULL;
    app->gui = NULL;

    // Free global configuration
    config_free(app->config);
    furi_string_free(app->config_file);
    free(app);

    B2F_LOG_I(APP_NAME, "Freed app.");

    return 0;
}

// Stub entrypoint due to gcc complaining about
// mismatching main function signature.
int32_t entrypoint(void* p) {
    UNUSED(p);
    return main();
}
