#pragma once

#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <gui/gui.h>
#include <storage/storage.h>

#include "scenes/import.h"
#include "config/config.h"

#define APP_NAME "Boot2Flipper"

// Text edited by the TextEdit and NameEdit scenes
typedef enum {
    EditTargetChainloadUrl,
    EditTargetChainloadArgs,
    EditTargetInterface,
    EditTargetProfileName,
} EditTarget;

#define APP_EDIT_BUFFER_SIZE 256

typedef struct App {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;

    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;

    void** allocated_scenes;

    FuriString* config_file; // Profile being edited, saved on changes
    Boot2FlipperConfig* config;

    EditTarget edit_target;
    char edit_buffer[APP_EDIT_BUFFER_SIZE];
} App;

/**
 * Enum for scenes.
 */
typedef enum {
#define SCENE_ACTION(scene) scene,
#include "scenes/list.h"
#undef SCENE_ACTION

    AppSceneNum, // This should be the last element in the enumeration.
} AppViews;

/**
 * Save the configuration to the current profile and remember it as the last one
 */
void app_save_config(App* app);

/**
 * Header definition for handler.c
 */
bool scene_handler_event_forwarder(void* context, uint32_t event_id);
bool scene_handler_navigation_forwarder(void* context);
void scene_handler_tick_forwarder(void* context);
