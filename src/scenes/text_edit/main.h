#pragma once

#include <furi.h>
#include <gui/view.h>
#include "../../views/url_input.h"

/**
 * Edits app->edit_target with the URL keyboard
 */
typedef struct {
    UrlInput* url_input;
} AppTextEdit;

AppTextEdit* TextEdit_alloc();
View* TextEdit_get_view(void* p);
void TextEdit_free(void* p);
void TextEdit_on_enter(void* p);
bool TextEdit_on_event(void* p, SceneManagerEvent e);
void TextEdit_on_exit(void* p);
