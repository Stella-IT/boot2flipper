#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/modules/text_input.h>

/**
 * Edits app->edit_target with the stock keyboard: names of files and interfaces
 */
typedef struct {
    TextInput* text_input;
} AppNameEdit;

AppNameEdit* NameEdit_alloc();
View* NameEdit_get_view(void* p);
void NameEdit_free(void* p);
void NameEdit_on_enter(void* p);
bool NameEdit_on_event(void* p, SceneManagerEvent e);
void NameEdit_on_exit(void* p);
