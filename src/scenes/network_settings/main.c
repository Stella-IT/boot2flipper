#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include "../../main.h"
#include "main.h"

#define THIS_SCENE NetworkSettings
#define TAG        "NetworkSettings"

// Forward declarations
static void NetworkSettings_build_menu(App* app);
static void NetworkSettings_enter_callback(void* context, uint32_t index);
static void NetworkSettings_ip_input_callback(void* context);

AppNetworkSettings* NetworkSettings_alloc() {
    AppNetworkSettings* settings = (AppNetworkSettings*)malloc(sizeof(AppNetworkSettings));
    settings->var_item_list = variable_item_list_alloc();
    settings->ip_input = ip_input_alloc();

    settings->ip_item = NULL;
    settings->subnet_item = NULL;
    settings->gateway_item = NULL;
    settings->dns_item = NULL;

    settings->editing_field = NETWORK_FIELD_IP;
    settings->current_view = NETWORK_VIEW_MAIN_LIST;

    return settings;
}

void NetworkSettings_free(void* ptr) {
    AppNetworkSettings* settings = (AppNetworkSettings*)ptr;
    if(settings == NULL) return;

    if(settings->ip_input != NULL) {
        ip_input_free(settings->ip_input);
    }

    if(settings->var_item_list != NULL) {
        variable_item_list_free(settings->var_item_list);
    }

    free(settings);
}

View* NetworkSettings_get_view(void* ptr) {
    AppNetworkSettings* settings = (AppNetworkSettings*)ptr;
    if(!settings) return NULL;

    switch(settings->current_view) {
    case NETWORK_VIEW_MAIN_LIST:
        return variable_item_list_get_view(settings->var_item_list);
    case NETWORK_VIEW_IP_INPUT:
        return ip_input_get_view(settings->ip_input);
    default:
        return variable_item_list_get_view(settings->var_item_list);
    }
}

static void NetworkSettings_build_menu(App* app) {
    if(!app || !app->config) return;

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(!settings) return;

    variable_item_list_reset(settings->var_item_list);

    // IP Address
    settings->ip_item =
        variable_item_list_add(settings->var_item_list, "IP Address", 0, NULL, NULL);
    if(settings->ip_item) {
        variable_item_set_current_value_text(
            settings->ip_item, furi_string_get_cstr(app->config->ip_addr));
    }

    // Subnet Mask
    settings->subnet_item =
        variable_item_list_add(settings->var_item_list, "Subnet Mask", 0, NULL, NULL);
    if(settings->subnet_item) {
        variable_item_set_current_value_text(
            settings->subnet_item, furi_string_get_cstr(app->config->subnet_mask));
    }

    // Gateway
    settings->gateway_item =
        variable_item_list_add(settings->var_item_list, "Gateway", 0, NULL, NULL);
    if(settings->gateway_item) {
        variable_item_set_current_value_text(
            settings->gateway_item, furi_string_get_cstr(app->config->gateway));
    }

    // DNS
    settings->dns_item = variable_item_list_add(settings->var_item_list, "DNS", 0, NULL, NULL);
    if(settings->dns_item) {
        variable_item_set_current_value_text(
            settings->dns_item, furi_string_get_cstr(app->config->dns));
    }

    variable_item_list_set_enter_callback(
        settings->var_item_list, NetworkSettings_enter_callback, app);
}

static void NetworkSettings_ip_input_callback(void* context) {
    App* app = (App*)context;
    if(!app || !app->config) return;

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(!settings) return;

    FuriString* ip_string = furi_string_alloc_printf(
        "%u.%u.%u.%u",
        settings->ip_octets[0],
        settings->ip_octets[1],
        settings->ip_octets[2],
        settings->ip_octets[3]);

    switch(settings->editing_field) {
    case NETWORK_FIELD_IP:
        furi_string_set(app->config->ip_addr, ip_string);
        break;
    case NETWORK_FIELD_SUBNET:
        furi_string_set(app->config->subnet_mask, ip_string);
        break;
    case NETWORK_FIELD_GATEWAY:
        furi_string_set(app->config->gateway, ip_string);
        break;
    case NETWORK_FIELD_DNS:
        furi_string_set(app->config->dns, ip_string);
        break;
    }

    furi_string_free(ip_string);

    settings->current_view = NETWORK_VIEW_MAIN_LIST;
    NetworkSettings_build_menu(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, NETWORK_VIEW_MAIN_LIST);
}

static void NetworkSettings_enter_callback(void* context, uint32_t index) {
    App* app = (App*)context;
    if(!app || !app->config) return;

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(!settings) return;

    settings->editing_field = (NetworkFieldType)index;

    // Get the appropriate string
    FuriString* field_string = NULL;
    const char* header = NULL;
    switch(index) {
    case NETWORK_FIELD_IP:
        field_string = app->config->ip_addr;
        header = "Enter IP Address";
        break;
    case NETWORK_FIELD_SUBNET:
        field_string = app->config->subnet_mask;
        header = "Enter Subnet Mask";
        break;
    case NETWORK_FIELD_GATEWAY:
        field_string = app->config->gateway;
        header = "Enter Gateway";
        break;
    case NETWORK_FIELD_DNS:
        field_string = app->config->dns;
        header = "Enter DNS Server";
        break;
    default:
        return;
    }

    // Parse existing IP; start empty if it isn't a valid address
    const char* current = furi_string_get_cstr(field_string);
    unsigned ip[4];
    bool valid = sscanf(current, "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3]) == 4;
    for(uint8_t i = 0; valid && i < 4; i++) {
        if(ip[i] > 255) valid = false;
    }
    if(valid) {
        for(uint8_t i = 0; i < 4; i++) {
            settings->ip_octets[i] = ip[i];
        }
    }

    ip_input_set_header_text(settings->ip_input, header);
    ip_input_set_result_callback(
        settings->ip_input, NetworkSettings_ip_input_callback, app, settings->ip_octets, valid);

    settings->current_view = NETWORK_VIEW_IP_INPUT;
    view_dispatcher_switch_to_view(app->view_dispatcher, NETWORK_VIEW_IP_INPUT);
}

void NetworkSettings_on_enter(void* context) {
    App* app = (App*)context;
    if(!app) return;

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(!settings) return;

    // Register both views only once
    static bool views_registered = false;
    if(!views_registered) {
        view_dispatcher_add_view(
            app->view_dispatcher,
            NETWORK_VIEW_MAIN_LIST,
            variable_item_list_get_view(settings->var_item_list));
        view_dispatcher_add_view(
            app->view_dispatcher, NETWORK_VIEW_IP_INPUT, ip_input_get_view(settings->ip_input));
        views_registered = true;
    }

    NetworkSettings_build_menu(app);

    settings->current_view = NETWORK_VIEW_MAIN_LIST;
    view_dispatcher_switch_to_view(app->view_dispatcher, NETWORK_VIEW_MAIN_LIST);
}

bool NetworkSettings_on_event(void* context, SceneManagerEvent event) {
    App* app = (App*)context;
    if(!app) return false;

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(!settings) return false;

    if(event.type == SceneManagerEventTypeBack) {
        if(settings->current_view == NETWORK_VIEW_IP_INPUT) {
            // Return to main list from IP input
            settings->current_view = NETWORK_VIEW_MAIN_LIST;
            view_dispatcher_switch_to_view(app->view_dispatcher, NETWORK_VIEW_MAIN_LIST);
            return true; // Consume the back event
        }
        // From main list, go back to Home scene
        return false; // Don't consume, let scene manager handle navigation
    }

    return true; // Handle other events
}

void NetworkSettings_on_exit(void* context) {
    App* app = (App*)context;
    if(app == NULL || app->allocated_scenes == NULL) {
        return;
    }

    AppNetworkSettings* settings = app->allocated_scenes[THIS_SCENE];
    if(settings == NULL) {
        return;
    }

    // Reset the variable item list but don't remove views
    // Views are registered once and remain registered
    variable_item_list_reset(settings->var_item_list);
}
