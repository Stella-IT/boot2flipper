#include "tab_button.h"

#define BUTTON_HEIGHT 13
#define BUTTON_RADIUS 2
#define BUTTON_TEXT_Y 61
#define BUTTON_ROWS   12 // Visible rows, icons are centered in them
#define BUTTON_EDGE   2 // Between the screen edge and the icon
#define BUTTON_SPACE  3 // Between the icon and the text, and after the text

void tab_button_draw(
    Canvas* canvas,
    bool right,
    const uint8_t* icon,
    uint8_t icon_width,
    uint8_t icon_height,
    const char* text) {
    const int32_t text_width = canvas_string_width(canvas, text);
    const int32_t width = 1 + BUTTON_EDGE + icon_width + BUTTON_SPACE + text_width + BUTTON_SPACE;
    const int32_t icon_y = TAB_BUTTON_Y + (BUTTON_ROWS - icon_height) / 2;

    canvas_set_color(canvas, ColorBlack);
    if(right) {
        const int32_t x = canvas_width(canvas) + 1 - width;
        canvas_draw_rbox(canvas, x, TAB_BUTTON_Y, width, BUTTON_HEIGHT, BUTTON_RADIUS);
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_str(canvas, x + BUTTON_SPACE, BUTTON_TEXT_Y, text);
        canvas_draw_xbm(
            canvas,
            canvas_width(canvas) - BUTTON_EDGE - icon_width,
            icon_y,
            icon_width,
            icon_height,
            icon);
    } else {
        canvas_draw_rbox(canvas, -1, TAB_BUTTON_Y, width, BUTTON_HEIGHT, BUTTON_RADIUS);
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_xbm(canvas, BUTTON_EDGE, icon_y, icon_width, icon_height, icon);
        canvas_draw_str(canvas, BUTTON_EDGE + icon_width + BUTTON_SPACE, BUTTON_TEXT_Y, text);
    }
    canvas_set_color(canvas, ColorBlack);
}
