#pragma once

#include <gui/view.h>

/**
 * IPv4 address input
 *
 * Shows all four octets at once with a digit keypad, instead of asking for
 * each octet on its own screen. Typing fills the focused octet and moves to the
 * next one when no more digits fit (e.g. after "192", "0" or "26").
 *
 * Keys: OK on a digit types it. The arrow keys move to the previous/next octet.
 * Backspace deletes a digit (a selected octet at once, then into the previous
 * octet); long OK on it clears all. Save submits. A newly focused octet is
 * replaced by typing. Long/repeat Back deletes, short Back leaves without saving.
 */
typedef struct IpInput IpInput;

typedef void (*IpInputCallback)(void* context);

/** Called on every key press, e.g. to see that the user is typing */
typedef void (*IpInputActivityCallback)(void* context);

IpInput* ip_input_alloc(void);

void ip_input_free(IpInput* ip_input);

View* ip_input_get_view(IpInput* ip_input);

void ip_input_set_header_text(IpInput* ip_input, const char* text);

/**
 * Edit an address; on Save, ip is written and callback is called
 * @param ip 4 octets, written on Save
 * @param prefill Start with the octets in ip instead of empty fields
 */
void ip_input_set_result_callback(
    IpInput* ip_input,
    IpInputCallback callback,
    void* callback_context,
    uint8_t* ip,
    bool prefill);

void ip_input_set_activity_callback(
    IpInput* ip_input,
    IpInputActivityCallback callback,
    void* context);
