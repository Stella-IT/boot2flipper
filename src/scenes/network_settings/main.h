#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include "../../views/ip_input.h"

typedef enum {
    NETWORK_FIELD_IP,
    NETWORK_FIELD_SUBNET,
    NETWORK_FIELD_GATEWAY,
    NETWORK_FIELD_DNS,
} NetworkFieldType;

typedef enum {
    NETWORK_VIEW_MAIN_LIST = 200,
    NETWORK_VIEW_IP_INPUT = 201,
} NetworkView;

typedef struct {
    VariableItemList* var_item_list;
    IpInput* ip_input;

    VariableItem* ip_item;
    VariableItem* subnet_item;
    VariableItem* gateway_item;
    VariableItem* dns_item;

    // Temporary storage for editing
    uint8_t ip_octets[4];
    NetworkFieldType editing_field;

    NetworkView current_view;
} AppNetworkSettings;

AppNetworkSettings* NetworkSettings_alloc();
View* NetworkSettings_get_view(void* p);
void NetworkSettings_free(void* p);
void NetworkSettings_on_enter(void* p);
bool NetworkSettings_on_event(void* p, SceneManagerEvent e);
void NetworkSettings_on_exit(void* p);
