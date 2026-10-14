#include <gui/view.h>
#include <gui/modules/variable_item_list.h>
#include "../../main.h"
#include "../../config/config.h"
#include "main.h"

#define THIS_SCENE Advanced

// Interfaces to pick with Left/Right, OK types any other name
static const char* const interface_names[] = {
    NETWORK_INTERFACE_AUTO,
    NETWORK_INTERFACE_ASK,
    "net0",
    "net1",
    "net2",
    "net3",
};
#define INTERFACE_NAME_COUNT COUNT_OF(interface_names)

static const char* no_yes_names[] = {"No", "Yes"};
static const char* b2f_quiet_names[] = {"normal", "quiet"};

AppAdvanced* Advanced_alloc() {
    AppAdvanced* advanced = malloc(sizeof(AppAdvanced));
    advanced->var_item_list = variable_item_list_alloc();
    advanced->interface_count = 0;
    return advanced;
}

void Advanced_free(void* ptr) {
    AppAdvanced* advanced = ptr;
    if(advanced == NULL) return;
    variable_item_list_free(advanced->var_item_list);
    free(advanced);
}

View* Advanced_get_view(void* ptr) {
    AppAdvanced* advanced = ptr;
    return advanced ? variable_item_list_get_view(advanced->var_item_list) : NULL;
}

static void Advanced_interface_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    AppAdvanced* advanced = app->allocated_scenes[THIS_SCENE];
    uint8_t index = variable_item_get_current_value_index(item);
    furi_string_set_str(app->config->network_interface, advanced->interfaces[index]);
    variable_item_set_current_value_text(item, advanced->interfaces[index]);
}

static void Advanced_status_report_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->status_report = index == 1;
    variable_item_set_current_value_text(item, no_yes_names[index]);
}

static void Advanced_b2f_quiet_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->b2f_quiet = index == 1;
    variable_item_set_current_value_text(item, b2f_quiet_names[index]);
}

static void Advanced_collect_sysinfo_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->collect_sysinfo = index == 1;
    variable_item_set_current_value_text(item, no_yes_names[index]);
}

static void Advanced_write_log_change(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->config->write_log = index == 1;
    variable_item_set_current_value_text(item, no_yes_names[index]);
}

static void Advanced_enter_callback(void* context, uint32_t index) {
    App* app = context;
    if(index == AdvancedItemInterface) {
        app->edit_target = EditTargetInterface;
        scene_manager_next_scene(app->scene_manager, NameEdit);
    }
}

static void Advanced_build(App* app) {
    AppAdvanced* advanced = app->allocated_scenes[THIS_SCENE];
    const Boot2FlipperConfig* config = app->config;
    variable_item_list_reset(advanced->var_item_list);

    // A typed name is offered after the usual ones
    const char* iface = furi_string_get_cstr(config->network_interface);
    uint8_t current = INTERFACE_NAME_COUNT;
    advanced->interface_count = 0;
    for(uint8_t i = 0; i < INTERFACE_NAME_COUNT; i++) {
        if(strcmp(iface, interface_names[i]) == 0) current = i;
        advanced->interfaces[advanced->interface_count++] = interface_names[i];
    }
    if(current == INTERFACE_NAME_COUNT) {
        strlcpy(advanced->custom_interface, iface, sizeof(advanced->custom_interface));
        advanced->interfaces[advanced->interface_count++] = advanced->custom_interface;
    }

    VariableItem* item = variable_item_list_add(
        advanced->var_item_list,
        "Net Interface",
        advanced->interface_count,
        Advanced_interface_change,
        app);
    variable_item_set_current_value_index(item, current);
    variable_item_set_current_value_text(item, advanced->interfaces[current]);

    item = variable_item_list_add(
        advanced->var_item_list, "Status report", 2, Advanced_status_report_change, app);
    variable_item_set_current_value_index(item, config->status_report);
    variable_item_set_current_value_text(item, no_yes_names[config->status_report]);

    item = variable_item_list_add(
        advanced->var_item_list, "b2f imgexec", 2, Advanced_b2f_quiet_change, app);
    variable_item_set_current_value_index(item, config->b2f_quiet);
    variable_item_set_current_value_text(item, b2f_quiet_names[config->b2f_quiet]);

    item = variable_item_list_add(
        advanced->var_item_list, "Collect sysinfo", 2, Advanced_collect_sysinfo_change, app);
    variable_item_set_current_value_index(item, config->collect_sysinfo);
    variable_item_set_current_value_text(item, no_yes_names[config->collect_sysinfo]);

    item = variable_item_list_add(
        advanced->var_item_list, "Write log", 2, Advanced_write_log_change, app);
    variable_item_set_current_value_index(item, config->write_log);
    variable_item_set_current_value_text(item, no_yes_names[config->write_log]);

    variable_item_list_set_enter_callback(advanced->var_item_list, Advanced_enter_callback, app);
}

void Advanced_on_enter(void* context) {
    App* app = context;
    Advanced_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, THIS_SCENE);
}

bool Advanced_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void Advanced_on_exit(void* context) {
    UNUSED(context);
}
