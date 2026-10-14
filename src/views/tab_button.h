#pragma once

#include <gui/canvas.h>

/**
 * Button in a bottom corner, as stock apps draw them: a box rounded at its
 * inner top corner (the rest is off screen), with the key's icon at the outer side
 * @param right true for the bottom right corner, false for the bottom left one
 * @param icon XBM icon of the key, up to TAB_BUTTON_HEIGHT - 1 rows
 */
void tab_button_draw(
    Canvas* canvas,
    bool right,
    const uint8_t* icon,
    uint8_t icon_width,
    uint8_t icon_height,
    const char* text);

// Top of the buttons, content above stays clear of them
#define TAB_BUTTON_Y 52
