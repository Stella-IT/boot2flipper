#pragma once

#include <gui/view.h>

/**
 * Text input with a URL keyboard
 *
 * The stock TextInput only has letters, digits and '_', so URLs can't be typed.
 * This keyboard has three layers (lowercase, uppercase, digits/symbols), with
 * '.', '/', ':' and '-' on the letter layers too.
 *
 * Keys: OK types, long OK types the other case (clears all on backspace, types a
 * space on the layer key),
 * long/repeat Back deletes, short Back leaves without saving.
 */
typedef struct UrlInput UrlInput;

typedef void (*UrlInputCallback)(void* context);

/** Called on every key press, e.g. to see that the user is typing */
typedef void (*UrlInputActivityCallback)(void* context);

UrlInput* url_input_alloc(void);

void url_input_free(UrlInput* url_input);

View* url_input_get_view(UrlInput* url_input);

void url_input_set_header_text(UrlInput* url_input, const char* text);

/**
 * Edit text_buffer in place; callback is called when OK is pressed on the Save key
 * @param text_buffer Buffer holding the initial text, edited in place
 * @param text_buffer_size Buffer size including the terminating NUL
 */
void url_input_set_result_callback(
    UrlInput* url_input,
    UrlInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size);

void url_input_set_activity_callback(
    UrlInput* url_input,
    UrlInputActivityCallback callback,
    void* context);

/** Show the digits and symbols layer, e.g. for numbers. Call after set_result_callback. */
void url_input_select_digits(UrlInput* url_input);
