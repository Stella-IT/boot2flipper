#include "confirm_input.h"
#include "key_icons.h"
#include "tab_button.h"
#include <gui/elements.h>
#include <string.h>

// Text centered above the buttons
#define TEXT_MARGIN      4
#define TEXT_LINE_HEIGHT 10
#define TEXT_CAP_HEIGHT  7 // FontSecondary, baseline to the top of capitals
#define TEXT_LINES       4

struct ConfirmInput {
    View* view;
    ConfirmInputCallback callback;
    void* callback_context;
    ConfirmInputActivityCallback activity_callback;
    void* activity_context;
};

typedef struct {
    const char* text;
    const char* left;
    const char* right;
    int32_t countdown; // Seconds, negative if hidden
} ConfirmInputModel;

// Left: Back key, right: OK key
static void confirm_input_draw_button(Canvas* canvas, bool right, const char* text) {
    if(right) {
        tab_button_draw(
            canvas, true, key_icon_ok_bits, KEY_ICON_OK_WIDTH, KEY_ICON_OK_HEIGHT, text);
    } else {
        tab_button_draw(
            canvas, false, key_icon_back_bits, KEY_ICON_BACK_WIDTH, KEY_ICON_BACK_HEIGHT, text);
    }
}

static void confirm_input_draw_callback(Canvas* canvas, void* _model) {
    ConfirmInputModel* model = _model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    // Text, one line per '\n', cut to the screen width, centered above the buttons
    const char* text = model->text ? model->text : "";
    uint8_t lines = 0;
    for(const char* p = text; *p != '\0' && lines < TEXT_LINES; lines++) {
        const char* end = strchr(p, '\n');
        p = end ? end + 1 : p + strlen(p);
    }
    const int32_t block_height = (lines - 1) * TEXT_LINE_HEIGHT + TEXT_CAP_HEIGHT;
    const int32_t first_baseline = (TAB_BUTTON_Y - block_height) / 2 + TEXT_CAP_HEIGHT;

    for(uint8_t line = 0; line < lines; line++) {
        const char* end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);

        char buffer[48];
        if(length >= sizeof(buffer)) length = sizeof(buffer) - 1;
        memcpy(buffer, text, length);
        buffer[length] = '\0';
        while(length > 0 &&
              canvas_string_width(canvas, buffer) > canvas_width(canvas) - TEXT_MARGIN * 2) {
            buffer[--length] = '\0';
        }
        canvas_draw_str_aligned(
            canvas,
            canvas_width(canvas) / 2,
            first_baseline + line * TEXT_LINE_HEIGHT,
            AlignCenter,
            AlignBottom,
            buffer);
        text = end ? end + 1 : text + strlen(text);
    }

    if(model->countdown >= 0) {
        char seconds[12];
        snprintf(seconds, sizeof(seconds), "%ld", model->countdown);
        canvas_draw_str_aligned(
            canvas, canvas_width(canvas) - 2, 1, AlignRight, AlignTop, seconds);
    }

    if(model->left) confirm_input_draw_button(canvas, false, model->left);
    if(model->right) confirm_input_draw_button(canvas, true, model->right);
}

static bool confirm_input_input_callback(InputEvent* event, void* context) {
    ConfirmInput* confirm_input = context;
    if(event->type == InputTypePress && confirm_input->activity_callback) {
        confirm_input->activity_callback(confirm_input->activity_context);
    }
    if(event->type != InputTypeShort) return false;
    if(event->key != InputKeyOk && event->key != InputKeyBack) return false;

    bool right = event->key == InputKeyOk;
    bool has_button = false;
    with_view_model(
        confirm_input->view,
        ConfirmInputModel * model,
        { has_button = right ? model->right != NULL : model->left != NULL; },
        false);

    // Without a left button, Back goes to the view dispatcher
    if(!has_button) return right;

    // Called outside the model lock, since the callback usually switches views
    if(confirm_input->callback) confirm_input->callback(confirm_input->callback_context, right);
    return true;
}

ConfirmInput* confirm_input_alloc(void) {
    ConfirmInput* confirm_input = malloc(sizeof(ConfirmInput));
    memset(confirm_input, 0, sizeof(ConfirmInput));

    confirm_input->view = view_alloc();
    view_set_context(confirm_input->view, confirm_input);
    view_allocate_model(confirm_input->view, ViewModelTypeLocking, sizeof(ConfirmInputModel));
    view_set_draw_callback(confirm_input->view, confirm_input_draw_callback);
    view_set_input_callback(confirm_input->view, confirm_input_input_callback);

    with_view_model(
        confirm_input->view,
        ConfirmInputModel * model,
        {
            memset(model, 0, sizeof(ConfirmInputModel));
            model->countdown = -1;
        },
        false);
    return confirm_input;
}

void confirm_input_free(ConfirmInput* confirm_input) {
    if(confirm_input == NULL) return;
    view_free(confirm_input->view);
    free(confirm_input);
}

View* confirm_input_get_view(ConfirmInput* confirm_input) {
    return confirm_input->view;
}

void confirm_input_set(
    ConfirmInput* confirm_input,
    const char* text,
    const char* left,
    const char* right) {
    with_view_model(
        confirm_input->view,
        ConfirmInputModel * model,
        {
            model->text = text;
            model->left = left;
            model->right = right;
        },
        true);
}

void confirm_input_set_countdown(ConfirmInput* confirm_input, int32_t seconds) {
    with_view_model(
        confirm_input->view, ConfirmInputModel * model, { model->countdown = seconds; }, true);
}

void confirm_input_set_activity_callback(
    ConfirmInput* confirm_input,
    ConfirmInputActivityCallback callback,
    void* context) {
    confirm_input->activity_callback = callback;
    confirm_input->activity_context = context;
}

void confirm_input_set_callback(
    ConfirmInput* confirm_input,
    ConfirmInputCallback callback,
    void* context) {
    confirm_input->callback = callback;
    confirm_input->callback_context = context;
}
