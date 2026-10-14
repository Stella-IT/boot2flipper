#pragma once

#include <gui/view.h>

/**
 * Confirm dialog
 *
 * Text above two buttons in the bottom corners, as stock apps draw them: the
 * left (negative, e.g. "No") pressed with Back, and the right (positive, e.g.
 * "Yes") pressed with OK. Without a left button, Back is not consumed.
 */
typedef struct ConfirmInput ConfirmInput;

/**
 * Callback for a pressed button
 * @param context Callback context
 * @param right true for the right button, false for the left one
 */
typedef void (*ConfirmInputCallback)(void* context, bool right);

ConfirmInput* confirm_input_alloc(void);

void confirm_input_free(ConfirmInput* confirm_input);

View* confirm_input_get_view(ConfirmInput* confirm_input);

/**
 * Set the text and buttons (not copied, they must stay valid while shown)
 * @param text Text, '\n' starts a new line
 * @param left Left button text, or NULL for no left button
 * @param right Right button text
 */
void confirm_input_set(
    ConfirmInput* confirm_input,
    const char* text,
    const char* left,
    const char* right);

void confirm_input_set_callback(
    ConfirmInput* confirm_input,
    ConfirmInputCallback callback,
    void* context);
