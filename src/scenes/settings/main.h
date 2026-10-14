#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/variable_item_list.h>

typedef enum {
    SettingsItemProfile,
    SettingsItemNetwork,
    SettingsItemIpSettings,
    SettingsItemChainload,
    SettingsItemUrl,
    SettingsItemArgs,
    SettingsItemScript,
    SettingsItemBootMethod,
    SettingsItemAdvanced,
    SettingsItemCount,
} SettingsItem;

typedef struct {
    VariableItemList* var_item_list;
    SettingsItem items[SettingsItemCount]; // Item of each shown row
    uint8_t item_count;
} AppSettings;

AppSettings* Settings_alloc();
View* Settings_get_view(void* p);
void Settings_free(void* p);
void Settings_on_enter(void* p);
bool Settings_on_event(void* p, SceneManagerEvent e);
void Settings_on_exit(void* p);
