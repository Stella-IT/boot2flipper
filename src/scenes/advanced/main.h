#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/variable_item_list.h>

typedef enum {
    AdvancedItemInterface,
    AdvancedItemStatusReport,
    AdvancedItemB2fQuiet,
    AdvancedItemCollectSysinfo,
    AdvancedItemWriteLog,
} AdvancedItem;

typedef struct {
    VariableItemList* var_item_list;
    const char* interfaces[8]; // Values of the interface item
    uint8_t interface_count;
    char custom_interface[32]; // A typed interface name
} AppAdvanced;

AppAdvanced* Advanced_alloc();
View* Advanced_get_view(void* p);
void Advanced_free(void* p);
void Advanced_on_enter(void* p);
bool Advanced_on_event(void* p, SceneManagerEvent e);
void Advanced_on_exit(void* p);
