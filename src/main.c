#include <stdio.h>
#include <furi.h>
#include <gui/gui.h>
#include "main.h"
#include "scenes/register.h"

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

    app->config_file = furi_string_alloc_printf("/ext/apps_data/boot2flipper/config.b2f");

    register_scenes(app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // The default scene is always the first one!
    scene_manager_next_scene(app->scene_manager, 0);

    view_dispatcher_run(app->view_dispatcher);

    FURI_LOG_I(APP_NAME, "Exiting application.");
    free_scenes(app);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);

    app->storage = NULL;
    app->dialogs = NULL;
    app->gui = NULL;

    // Free global configuration
    config_free(app->config);

    FURI_LOG_I(APP_NAME, "Freed app.");

    return 0;
}

// Stub entrypoint due to gcc complaining about
// mismatching main function signature.
int32_t entrypoint(void* p) {
    UNUSED(p);
    return main();
}
