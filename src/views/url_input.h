#pragma once

#include <gui/view.h>

/**
 * Text input with a URL keyboard
 *
 * The stock TextInput only has letters, digits and '_', so URLs can't be typed.
 * This keyboard has three layers (lowercase, uppercase, digits/symbols), with
 * '.', '/', ':' and '-' on the letter layers too.
 *
 * Keys: OK types, long OK types the other case (or clears all on backspace),
 * long/repeat Back deletes, short Back leaves without saving.
 */
typedef struct UrlInput UrlInput;

typedef void (*UrlInputCallback)(void* context);

UrlInput* url_input_alloc(void);

void url_input_free(UrlInput* url_input);

View* url_input_get_view(UrlInput* url_input);

void url_input_set_header_text(UrlInput* url_input, const char* text);

/**
 * Edit text_buffer in place; callback is called when OK is pressed on the save key
 * @param text_buffer Buffer holding the initial text, edited in place
 * @param text_buffer_size Buffer size including the terminating NUL
 */
void url_input_set_result_callback(
    UrlInput* url_input,
    UrlInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size);
