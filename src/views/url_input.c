#include "url_input.h"
#include <gui/elements.h>
#include <ctype.h>
#include <string.h>

#define KEY_ROWS      3
#define KEY_COLUMNS   12 // Character keys per row
#define KEY_WIDTH     8
#define KEY_HEIGHT    11
#define KEYS_X        1
#define KEYS_Y        27 // Top of the first key row
#define SPECIAL_WIDTH 26
#define SPECIAL_X     (127 - SPECIAL_WIDTH)
#define SPECIAL_COL   KEY_COLUMNS // Column index of the backspace/layer/save keys

typedef enum {
    UrlInputLayerLower,
    UrlInputLayerUpper,
    UrlInputLayerSymbol,
    UrlInputLayerCount,
} UrlInputLayer;

// Each row is exactly KEY_COLUMNS characters
static const char* const layer_keys[UrlInputLayerCount][KEY_ROWS] = {
    [UrlInputLayerLower] = {"qwertyuiop./", "asdfghjkl:-_", "zxcvbnm?=&#%"},
    [UrlInputLayerUpper] = {"QWERTYUIOP./", "ASDFGHJKL:-_", "ZXCVBNM?=&#%"},
    [UrlInputLayerSymbol] = {"1234567890./", ":-_~?=&#%+@!", "${}[]()*,;'\""},
};

// Label of the layer key: the layer it switches to
static const char* const layer_labels[UrlInputLayerCount] = {"ABC", "123", "abc"};

typedef enum {
    SpecialKeyDelete,
    SpecialKeyLayer,
    SpecialKeyEnter,
} SpecialKey; // Indexed by row

struct UrlInput {
    View* view;
    UrlInputCallback callback;
    void* callback_context;
};

typedef struct {
    const char* header;
    char* text;
    size_t text_size;
    uint8_t row;
    uint8_t col;
    UrlInputLayer layer;
} UrlInputModel;

static void url_input_draw_text(Canvas* canvas, UrlInputModel* model) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, model->header ? model->header : "");
    elements_slightly_rounded_frame(canvas, 1, 11, 126, 14);

    const char* text = model->text ? model->text : "";
    const uint16_t max_width = 118;
    const char* visible = text;
    int32_t x = 4;

    // Show the end of the text, where typing happens
    if(canvas_string_width(canvas, text) > max_width) {
        uint16_t ellipsis_width = canvas_string_width(canvas, "...");
        while(*visible && canvas_string_width(canvas, visible) > max_width - ellipsis_width) {
            visible++;
        }
        canvas_draw_str(canvas, x, 21, "...");
        x += ellipsis_width;
    }
    canvas_draw_str(canvas, x, 21, visible);

    int32_t cursor_x = x + canvas_string_width(canvas, visible) + 1;
    canvas_draw_line(canvas, cursor_x, 14, cursor_x, 22);
}

// Left arrow with a filled head and a 2px shaft on rows y - 1 and y
static void url_input_draw_backspace_arrow(Canvas* canvas, int32_t x, int32_t y) {
    for(int32_t i = 0; i < 3; i++) {
        canvas_draw_line(canvas, x + i, y - 1 - i, x + i, y + i);
    }
    canvas_draw_box(canvas, x + 3, y - 1, 10, 2);
}

static void url_input_draw_callback(Canvas* canvas, void* _model) {
    UrlInputModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    url_input_draw_text(canvas, model);

    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < KEY_ROWS; row++) {
        const char* keys = layer_keys[model->layer][row];
        int32_t y = KEYS_Y + row * (KEY_HEIGHT + 1);

        for(uint8_t col = 0; col < KEY_COLUMNS; col++) {
            int32_t x = KEYS_X + col * KEY_WIDTH;
            bool selected = model->row == row && model->col == col;
            if(selected) {
                canvas_draw_box(canvas, x, y, KEY_WIDTH, KEY_HEIGHT);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_glyph(canvas, x + 2, y + 9, keys[col]);
            canvas_set_color(canvas, ColorBlack);
        }
    }

    // Backspace, layer and save keys on the right, styled like the stock keyboard
    for(uint8_t row = 0; row < KEY_ROWS; row++) {
        int32_t y = KEYS_Y + row * (KEY_HEIGHT + 1);
        bool selected = model->row == row && model->col == SPECIAL_COL;

        if(selected) {
            canvas_draw_rbox(canvas, SPECIAL_X, y, SPECIAL_WIDTH, KEY_HEIGHT, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, SPECIAL_X, y, SPECIAL_WIDTH, KEY_HEIGHT, 2);
        }

        canvas_set_font(canvas, FontSecondary);
        if(row == SpecialKeyDelete) {
            url_input_draw_backspace_arrow(canvas, SPECIAL_X + 7, y + 6);
        } else if(row == SpecialKeyLayer) {
            canvas_draw_str_aligned(
                canvas,
                SPECIAL_X + SPECIAL_WIDTH / 2,
                y + 9,
                AlignCenter,
                AlignBottom,
                layer_labels[model->layer]);
        } else {
            // No ascenders, so sit one pixel higher to look centered
            canvas_draw_str_aligned(
                canvas, SPECIAL_X + SPECIAL_WIDTH / 2, y + 8, AlignCenter, AlignBottom, "save");
        }
        canvas_set_color(canvas, ColorBlack);
    }
}

static void url_input_backspace(UrlInputModel* model) {
    size_t length = strlen(model->text);
    if(length > 0) model->text[length - 1] = '\0';
}

static void url_input_insert(UrlInputModel* model, char ch) {
    size_t length = strlen(model->text);
    if(length + 1 < model->text_size) {
        model->text[length] = ch;
        model->text[length + 1] = '\0';
    }
}

// Returns true if the text was submitted
static bool url_input_press(UrlInputModel* model, bool long_press) {
    if(model->col == SPECIAL_COL) {
        switch(model->row) {
        case SpecialKeyDelete:
            if(long_press) {
                model->text[0] = '\0';
            } else {
                url_input_backspace(model);
            }
            return false;
        case SpecialKeyLayer:
            model->layer = (model->layer + 1) % UrlInputLayerCount;
            return false;
        default:
            return true;
        }
    }

    char ch = layer_keys[model->layer][model->row][model->col];
    if(long_press && isalpha((unsigned char)ch)) {
        ch = islower((unsigned char)ch) ? toupper((unsigned char)ch) : tolower((unsigned char)ch);
    }
    url_input_insert(model, ch);
    return false;
}

static void url_input_move(UrlInputModel* model, InputKey key) {
    switch(key) {
    case InputKeyUp:
        if(model->row > 0) model->row--;
        break;
    case InputKeyDown:
        if(model->row < KEY_ROWS - 1) model->row++;
        break;
    case InputKeyLeft:
        model->col = model->col > 0 ? model->col - 1 : SPECIAL_COL;
        break;
    case InputKeyRight:
        model->col = model->col < SPECIAL_COL ? model->col + 1 : 0;
        break;
    default:
        break;
    }
}

static bool url_input_input_callback(InputEvent* event, void* context) {
    UrlInput* url_input = context;
    bool consumed = false;
    bool submit = false;

    // Short Back falls through to the view dispatcher (leave without saving)
    if(event->key == InputKeyBack && event->type != InputTypeLong &&
       event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        url_input->view,
        UrlInputModel * model,
        {
            if(model->text != NULL) {
                consumed = true;
                if(event->key == InputKeyBack) {
                    url_input_backspace(model);
                } else if(event->key == InputKeyOk) {
                    if(event->type == InputTypeShort) {
                        submit = url_input_press(model, false);
                    } else if(event->type == InputTypeLong) {
                        submit = url_input_press(model, true);
                    } else if(
                        event->type == InputTypeRepeat && model->col == SPECIAL_COL &&
                        model->row == SpecialKeyDelete) {
                        url_input_backspace(model);
                    }
                } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                    url_input_move(model, event->key);
                }
            }
        },
        consumed);

    // Called outside the model lock, since the callback usually switches views
    if(submit && url_input->callback) {
        url_input->callback(url_input->callback_context);
    }
    return consumed;
}

UrlInput* url_input_alloc(void) {
    UrlInput* url_input = malloc(sizeof(UrlInput));
    memset(url_input, 0, sizeof(UrlInput));

    url_input->view = view_alloc();
    view_set_context(url_input->view, url_input);
    view_allocate_model(url_input->view, ViewModelTypeLocking, sizeof(UrlInputModel));
    view_set_draw_callback(url_input->view, url_input_draw_callback);
    view_set_input_callback(url_input->view, url_input_input_callback);

    with_view_model(
        url_input->view,
        UrlInputModel * model,
        { memset(model, 0, sizeof(UrlInputModel)); },
        false);

    return url_input;
}

void url_input_free(UrlInput* url_input) {
    if(url_input == NULL) return;
    view_free(url_input->view);
    free(url_input);
}

View* url_input_get_view(UrlInput* url_input) {
    return url_input->view;
}

void url_input_set_header_text(UrlInput* url_input, const char* text) {
    with_view_model(url_input->view, UrlInputModel * model, { model->header = text; }, true);
}

void url_input_set_result_callback(
    UrlInput* url_input,
    UrlInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size) {
    url_input->callback = callback;
    url_input->callback_context = callback_context;

    with_view_model(
        url_input->view,
        UrlInputModel * model,
        {
            model->text = text_buffer;
            model->text_size = text_buffer_size;
            model->row = 0;
            model->col = 0;
            model->layer = UrlInputLayerLower;
        },
        true);
}
