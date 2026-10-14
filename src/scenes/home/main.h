#pragma once

#include <furi.h>
#include <gui/view.h>

/**
 * Dashboard: the profile's settings at a glance, Left opens the settings,
 * OK starts the USB disk and Back exits
 */
typedef struct {
    View* view;
} AppHome;

AppHome* Home_alloc();
View* Home_get_view(void* p);
void Home_free(void* p);
void Home_on_enter(void* p);
bool Home_on_event(void* p, SceneManagerEvent e);
void Home_on_exit(void* p);
