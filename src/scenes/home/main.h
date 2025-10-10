#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/file_browser.h>
#include <dialogs/dialogs.h>

typedef enum {
    HOME_MENU_ITEM_LOAD,
    HOME_MENU_ITEM_SAVE,
    HOME_MENU_ITEM_NETWORK,
    HOME_MENU_ITEM_NETWORK_SETTINGS,
    HOME_MENU_ITEM_NETWORK_INTERFACE,
    HOME_MENU_ITEM_PARTITION_SCHEME,
    HOME_MENU_ITEM_CHAINLOAD_ENABLED,
    HOME_MENU_ITEM_CHAINLOAD_URL,
    HOME_MENU_ITEM_START,
} HomeMenuItem;

typedef enum {
    HOME_VIEW_MAIN_LIST = 100,
    HOME_VIEW_TEXT_INPUT = 101,
    HOME_VIEW_FILE_BROWSER = 102,
} HomeView;

typedef enum {
    HomeEventShowFileDialog,
} HomeCustomEvent;

typedef struct {
    VariableItemList* var_item_list;
    TextInput* text_input;
    FileBrowser* file_browser;
    FuriString* browser_result;

    VariableItem* network_item;
    VariableItem* network_settings_item;
    VariableItem* network_interface_item;
    VariableItem* partition_scheme_item;
    VariableItem* chainload_enabled_item;
    VariableItem* chainload_url_item;

    HomeView current_view;
    char text_buffer[128];
    bool is_save_mode;
    bool is_network_interface_mode;
} AppHome;

AppHome* Home_alloc();
View* Home_get_view(void* p);
void Home_free(void* p);
void Home_on_enter(void* p);
bool Home_on_event(void* p, SceneManagerEvent e);
void Home_on_exit(void* p);
