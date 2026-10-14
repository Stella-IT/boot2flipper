#include "ip_input.h"
#include "key_icons.h"
#include <gui/elements.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OCTETS        4
#define OCTET_DIGITS  3
#define OCTET_X       2
#define OCTET_Y       14
#define OCTET_WIDTH   28
#define OCTET_HEIGHT  13
#define OCTET_STEP    32 // Leaves a 4px gap for the dot
#define KEY_ROWS      2
#define DIGIT_COLUMNS 5
#define DIGIT_X       2
#define DIGIT_WIDTH   14
#define DIGIT_STEP    15
#define KEY_HEIGHT    13
#define KEYS_Y        32 // Top of the first key row
#define KEY_ROW_STEP  16
#define BACKSPACE_X   84
#define SAVE_X        (BACKSPACE_X + KEY_ICON_WIDTH + 2)
#define ARROW_SIZE    11 // Square prev/next buttons, centered above backspace and save
#define PREV_X        (BACKSPACE_X + (KEY_ICON_WIDTH - ARROW_SIZE) / 2)
#define NEXT_X        (SAVE_X + (KEY_ICON_SAVE_WIDTH - ARROW_SIZE) / 2)
#define COLUMNS       (DIGIT_COLUMNS + 2) // Digits, prev/backspace, next/save
#define COL_LEFT      DIGIT_COLUMNS // Prev on the first row, backspace on the second
#define COL_RIGHT     (DIGIT_COLUMNS + 1) // Next on the first row, save on the second

struct IpInput {
    View* view;
    IpInputCallback callback;
    void* callback_context;
};

typedef struct {
    const char* header;
    uint8_t* ip;
    char octets[OCTETS][OCTET_DIGITS + 1];
    uint8_t focus; // Octet being edited
    bool replace; // Focused octet is replaced by the next digit
    bool auto_advanced; // Focus moved after a full octet; ignore the next "next"
    uint8_t row;
    uint8_t col;
} IpInputModel;

static void ip_input_draw_octets(Canvas* canvas, IpInputModel* model) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < OCTETS; i++) {
        int32_t x = OCTET_X + i * OCTET_STEP;
        int32_t text_x = x + OCTET_WIDTH / 2;

        if(model->focus == i) {
            canvas_draw_rbox(canvas, x, OCTET_Y, OCTET_WIDTH, OCTET_HEIGHT, 1);
            canvas_set_color(canvas, ColorWhite);
            if(!model->replace || model->octets[i][0] == '\0') {
                // Typing appends: show a cursor after the digits
                int32_t cursor_x = text_x + canvas_string_width(canvas, model->octets[i]) / 2 + 2;
                canvas_draw_line(canvas, cursor_x, OCTET_Y + 3, cursor_x, OCTET_Y + 9);
            }
        } else {
            canvas_draw_rframe(canvas, x, OCTET_Y, OCTET_WIDTH, OCTET_HEIGHT, 1);
        }
        canvas_draw_str_aligned(
            canvas,
            text_x,
            OCTET_Y + OCTET_HEIGHT / 2 + 1,
            AlignCenter,
            AlignCenter,
            model->octets[i]);
        canvas_set_color(canvas, ColorBlack);

        if(i + 1 < OCTETS) {
            canvas_draw_box(canvas, x + OCTET_WIDTH + 1, OCTET_Y + OCTET_HEIGHT - 3, 2, 2);
        }
    }
}

static void ip_input_draw_callback(Canvas* canvas, void* _model) {
    IpInputModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, model->header ? model->header : "");

    ip_input_draw_octets(canvas, model);

    // Digit keys
    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0; row < KEY_ROWS; row++) {
        int32_t y = KEYS_Y + row * KEY_ROW_STEP;
        for(uint8_t col = 0; col < DIGIT_COLUMNS; col++) {
            int32_t x = DIGIT_X + col * DIGIT_STEP;
            char digit[2] = {'0' + row * DIGIT_COLUMNS + col, '\0'};
            if(model->row == row && model->col == col) {
                canvas_draw_rbox(canvas, x, y, DIGIT_WIDTH, KEY_HEIGHT, 1);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str_aligned(
                canvas,
                x + DIGIT_WIDTH / 2,
                y + KEY_HEIGHT / 2 + 1,
                AlignCenter,
                AlignCenter,
                digit);
            canvas_set_color(canvas, ColorBlack);
        }
    }

    // Prev and next buttons
    for(uint8_t col = COL_LEFT; col <= COL_RIGHT; col++) {
        int32_t x = col == COL_LEFT ? PREV_X : NEXT_X;
        int32_t y = KEYS_Y + (KEY_HEIGHT - ARROW_SIZE) / 2;
        if(model->row == 0 && model->col == col) {
            canvas_draw_rbox(canvas, x, y, ARROW_SIZE, ARROW_SIZE, 1);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, y, ARROW_SIZE, ARROW_SIZE, 1);
        }
        canvas_draw_xbm(
            canvas,
            x + (ARROW_SIZE - KEY_ICON_ARROW_WIDTH) / 2,
            y + (ARROW_SIZE - KEY_ICON_ARROW_HEIGHT) / 2,
            KEY_ICON_ARROW_WIDTH,
            KEY_ICON_ARROW_HEIGHT,
            col == COL_LEFT ? key_icon_left_small_bits : key_icon_right_small_bits);
        canvas_set_color(canvas, ColorBlack);
    }

    // Backspace and save keys
    int32_t row_y = KEYS_Y + KEY_ROW_STEP;
    bool selected = model->row == 1 && model->col == COL_LEFT;
    canvas_draw_xbm(
        canvas,
        BACKSPACE_X,
        row_y + (KEY_HEIGHT - KEY_ICON_HEIGHT) / 2,
        KEY_ICON_WIDTH,
        KEY_ICON_HEIGHT,
        selected ? key_icon_backspace_selected_bits : key_icon_backspace_bits);
    selected = model->row == 1 && model->col == COL_RIGHT;
    canvas_draw_xbm(
        canvas,
        SAVE_X,
        row_y + (KEY_HEIGHT - KEY_ICON_SAVE_HEIGHT) / 2,
        KEY_ICON_SAVE_WIDTH,
        KEY_ICON_SAVE_HEIGHT,
        selected ? key_icon_save_selected_bits : key_icon_save_bits);
}

static void ip_input_focus(IpInputModel* model, uint8_t octet) {
    model->focus = octet;
    model->replace = true;
}

static void ip_input_type(IpInputModel* model, char digit) {
    char* octet = model->octets[model->focus];
    if(model->replace || strcmp(octet, "0") == 0) {
        octet[0] = '\0'; // No leading zeros
    }
    model->replace = false;

    size_t length = strlen(octet);
    if(length >= OCTET_DIGITS) return;
    unsigned value = atoi(octet) * 10 + (digit - '0');
    if(value > 255) return;
    octet[length] = digit;
    octet[length + 1] = '\0';

    // Move on once no other digit can follow
    bool full = length + 1 == OCTET_DIGITS || value == 0 || value * 10 > 255;
    if(full && model->focus + 1 < OCTETS) {
        ip_input_focus(model, model->focus + 1);
        model->auto_advanced = true;
    }
}

static void ip_input_backspace(IpInputModel* model) {
    char* octet = model->octets[model->focus];
    if(model->replace) {
        octet[0] = '\0'; // Clear a selected octet at once
    } else if(octet[0] != '\0') {
        octet[strlen(octet) - 1] = '\0';
    } else if(model->focus > 0) {
        model->focus--; // Keep deleting into the previous octet
    }
    model->replace = false;
}

// Returns true if the address was submitted
static bool ip_input_save(IpInputModel* model) {
    for(uint8_t i = 0; i < OCTETS; i++) {
        if(model->octets[i][0] == '\0') {
            model->focus = i;
            model->replace = false;
            return false;
        }
    }
    for(uint8_t i = 0; i < OCTETS; i++) {
        model->ip[i] = atoi(model->octets[i]);
    }
    return true;
}

static void ip_input_clear(IpInputModel* model) {
    for(uint8_t i = 0; i < OCTETS; i++) {
        model->octets[i][0] = '\0';
    }
    ip_input_focus(model, 0);
}

// Returns true if the address was submitted
static bool ip_input_press(IpInputModel* model, bool long_press) {
    bool auto_advanced = model->auto_advanced;
    model->auto_advanced = false;

    if(model->col < DIGIT_COLUMNS) {
        if(!long_press) ip_input_type(model, '0' + model->row * DIGIT_COLUMNS + model->col);
    } else if(model->row == 1 && model->col == COL_LEFT) {
        if(long_press) {
            ip_input_clear(model);
        } else {
            ip_input_backspace(model);
        }
    } else if(model->row == 1) {
        return !long_press && ip_input_save(model);
    } else if(long_press) {
        return false;
    } else if(model->col == COL_LEFT) {
        ip_input_focus(model, (model->focus + OCTETS - 1) % OCTETS);
    } else if(!auto_advanced) {
        // After "192" the focus already moved on, so a habitual "next" is ignored
        ip_input_focus(model, (model->focus + 1) % OCTETS);
    }
    return false;
}

static void ip_input_move(IpInputModel* model, InputKey key) {
    switch(key) {
    case InputKeyUp:
    case InputKeyDown:
        model->row = !model->row;
        break;
    case InputKeyLeft:
        model->col = model->col > 0 ? model->col - 1 : COLUMNS - 1;
        break;
    case InputKeyRight:
        model->col = model->col + 1 < COLUMNS ? model->col + 1 : 0;
        break;
    default:
        break;
    }
}

static bool ip_input_input_callback(InputEvent* event, void* context) {
    IpInput* ip_input = context;
    bool consumed = false;
    bool submit = false;

    // Short Back falls through to the view dispatcher (leave without saving)
    if(event->key == InputKeyBack && event->type != InputTypeLong &&
       event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        ip_input->view,
        IpInputModel * model,
        {
            if(model->ip != NULL) {
                consumed = true;
                if(event->key == InputKeyBack) {
                    model->auto_advanced = false;
                    ip_input_backspace(model);
                } else if(event->key == InputKeyOk) {
                    if(event->type == InputTypeShort) {
                        submit = ip_input_press(model, false);
                    } else if(event->type == InputTypeLong) {
                        ip_input_press(model, true);
                    } else if(
                        event->type == InputTypeRepeat && model->row == 1 &&
                        model->col == COL_LEFT) {
                        ip_input_backspace(model);
                    }
                } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                    ip_input_move(model, event->key);
                }
            }
        },
        consumed);

    // Called outside the model lock, since the callback usually switches views
    if(submit && ip_input->callback) {
        ip_input->callback(ip_input->callback_context);
    }
    return consumed;
}

IpInput* ip_input_alloc(void) {
    IpInput* ip_input = malloc(sizeof(IpInput));
    memset(ip_input, 0, sizeof(IpInput));

    ip_input->view = view_alloc();
    view_set_context(ip_input->view, ip_input);
    view_allocate_model(ip_input->view, ViewModelTypeLocking, sizeof(IpInputModel));
    view_set_draw_callback(ip_input->view, ip_input_draw_callback);
    view_set_input_callback(ip_input->view, ip_input_input_callback);

    with_view_model(
        ip_input->view, IpInputModel * model, { memset(model, 0, sizeof(IpInputModel)); }, false);

    return ip_input;
}

void ip_input_free(IpInput* ip_input) {
    if(ip_input == NULL) return;
    view_free(ip_input->view);
    free(ip_input);
}

View* ip_input_get_view(IpInput* ip_input) {
    return ip_input->view;
}

void ip_input_set_header_text(IpInput* ip_input, const char* text) {
    with_view_model(ip_input->view, IpInputModel * model, { model->header = text; }, true);
}

void ip_input_set_result_callback(
    IpInput* ip_input,
    IpInputCallback callback,
    void* callback_context,
    uint8_t* ip,
    bool prefill) {
    ip_input->callback = callback;
    ip_input->callback_context = callback_context;

    with_view_model(
        ip_input->view,
        IpInputModel * model,
        {
            model->ip = ip;
            for(uint8_t i = 0; i < OCTETS; i++) {
                if(prefill) {
                    snprintf(model->octets[i], sizeof(model->octets[i]), "%u", ip[i]);
                } else {
                    model->octets[i][0] = '\0';
                }
            }
            ip_input_focus(model, 0);
            model->auto_advanced = false;
            model->row = 0;
            model->col = 0;
        },
        true);
}
