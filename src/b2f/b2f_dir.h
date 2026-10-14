#pragma once

#include "../disk/virtual_fat.h"

/**
 * .B2F directory: files the booted host reads and writes by LBA
 *
 * The directory and files sit at fixed LBAs in an area reserved right after the
 * root directory (0x2000), each sized to its file, and other files start after
 * the area. A program on the host (e.g. started from iPXE) can use them with raw
 * sector reads/writes, without parsing FAT. The directory is hidden and system,
 * like Windows' own hidden folders.
 *
 *   .B2F/info    B2F_INFO_LBA, read-only. Starts with B2F_MAGIC, so it can also
 *                be found by scanning the disk:
 *                  0  char[8]  B2F_MAGIC
 *                  8  u16      B2F_INFO_VERSION
 *                 10  u16      entry count
 *                 16  entries of 32 bytes: char[16] name (NUL padded),
 *                     u32 lba, u32 size in bytes, u32 flags (B2F_INFO_WRITABLE),
 *                     u32 reserved
 *                All numbers are little-endian.
 *   .B2F/mode    B2F_MODE_LBA, B2F_MODE_SIZE bytes
 *                What the Flipper shows: an ASCII word, up to the first NUL,
 *                space or newline. B2F_MODE_STATUS (the default, also for
 *                unknown words), B2F_MODE_FRAMEBUFFER or B2F_MODE_PROMPT.
 *                Writing B2F_MODE_PROMPT asks the prompt below; once answered
 *                the Flipper writes B2F_MODE_STATUS back.
 *   .B2F/status  B2F_STATUS_LBA, B2F_STATUS_SIZE bytes
 *                ASCII text shown in status mode, up to the first NUL.
 *   .B2F/frame   B2F_FRAME_LBA, B2F_FRAME_SIZE bytes
 *                128x64 1-bit frame shown full screen in framebuffer mode.
 *                Rows of 16 bytes from the top, least significant bit is the
 *                leftmost pixel (XBM, as canvas_draw_xbm takes), 1 is black.
 *   .B2F/prompt/ A question for the Flipper's user, asked when mode is set to
 *                B2F_MODE_PROMPT. The host writes:
 *                  type      "choice" (pick an option, the default), "confirm"
 *                            (two buttons) or "text"
 *                  question  Text shown with the options or keyboard. Lists and
 *                            keyboards show its first line.
 *                  options   One per line. Confirm: the positive (OK key) then
 *                            the negative (Back key) button, Yes/No if none,
 *                            only OK if one. Text: the default. A choice
 *                            without options is a Yes/No confirm.
 *                  kbdtype   Keyboard for text: "text" (default), "ip" or
 *                            "number"
 *                  timeout   Seconds to answer, counted down on the Flipper.
 *                            Empty or 0 waits forever.
 *                and reads back (read-only):
 *                  answer    Empty before the first prompt, "pending\n" until
 *                            answered, then "ok\n<value>\n" (the option or the
 *                            text), "cancel\n" or "timeout\n"
 *   .B2F/bin/b2f.efi, .B2F/bin/b2f.pxe
 *                Write the files from iPXE on UEFI and BIOS, see tools/b2f_efi
 *                and tools/b2f_pxe. The BIOS loader passes b2f.pxe as initrd.
 *
 * The sectors left before .B2F/bin are kept for more files.
 */
#define B2F_DIR_PATH      ".B2F"
#define B2F_BIN_PATH      B2F_DIR_PATH "/bin"
#define B2F_EFI_PATH      B2F_BIN_PATH "/b2f.efi"
#define B2F_PXE_PATH      B2F_BIN_PATH "/b2f.pxe"
#define B2F_MAGIC         "B2F INFO"
#define B2F_INFO_VERSION  1
#define B2F_INFO_WRITABLE 0x01

#define B2F_MODE_STATUS      "status"
#define B2F_MODE_FRAMEBUFFER "framebuffer"
#define B2F_MODE_PROMPT      "prompt"
#define B2F_PROMPT_PATH      B2F_DIR_PATH "/prompt"

// Fixed LBAs, part of the host interface (see README). 0x2001-0x20FF is
// reserved for .B2F, other files start at B2F_AREA_END_LBA.
#define B2F_DIR_LBA     0x2001
#define B2F_INFO_LBA    (B2F_DIR_LBA + 1)
#define B2F_STATUS_LBA  (B2F_INFO_LBA + 1)
#define B2F_STATUS_SIZE SECTOR_SIZE
#define B2F_FRAME_LBA   (B2F_STATUS_LBA + B2F_STATUS_SIZE / SECTOR_SIZE)
#define B2F_FRAME_SIZE  (128 * 64 / 8)
#define B2F_MODE_LBA    (B2F_FRAME_LBA + B2F_FRAME_SIZE / SECTOR_SIZE)
#define B2F_MODE_SIZE   SECTOR_SIZE

#define B2F_PROMPT_LBA         0x2010 // .B2F/prompt/
#define B2F_PROMPT_TYPE_LBA    (B2F_PROMPT_LBA + 1)
#define B2F_QUESTION_LBA       (B2F_PROMPT_TYPE_LBA + 1)
#define B2F_QUESTION_SIZE      SECTOR_SIZE
#define B2F_OPTIONS_LBA        (B2F_QUESTION_LBA + 1)
#define B2F_OPTIONS_SIZE       (2 * SECTOR_SIZE)
#define B2F_KBDTYPE_LBA        (B2F_OPTIONS_LBA + B2F_OPTIONS_SIZE / SECTOR_SIZE)
#define B2F_ANSWER_LBA         (B2F_KBDTYPE_LBA + 1)
#define B2F_TIMEOUT_LBA        (B2F_ANSWER_LBA + 1)
#define B2F_PROMPT_FILES_SIZE  SECTOR_SIZE // type, kbdtype, answer and timeout
#define B2F_PROMPT_TIMEOUT_MAX 3600 // Seconds

#define B2F_BIN_LBA      0x2020
#define B2F_AREA_END_LBA 0x2100

// Clusters are 1 sector, so every LBA in the data area is a cluster
_Static_assert(SECTORS_PER_CLUSTER == 1, ".B2F LBAs need 1 sector clusters");
_Static_assert(B2F_DIR_LBA == DATA_START_LBA + 1, ".B2F must follow the root directory");
_Static_assert(B2F_MODE_LBA + B2F_MODE_SIZE / SECTOR_SIZE <= B2F_PROMPT_LBA, ".B2F/mode too big");
_Static_assert(B2F_TIMEOUT_LBA < B2F_BIN_LBA, ".B2F/prompt runs into .B2F/bin");

typedef struct B2fDir B2fDir;

typedef enum {
    B2fModeStatus,
    B2fModeFramebuffer,
    B2fModePrompt,
} B2fMode;

typedef enum {
    B2fPromptChoice,
    B2fPromptConfirm,
    B2fPromptText,
} B2fPromptType;

typedef enum {
    B2fKeyboardText,
    B2fKeyboardIp,
    B2fKeyboardNumber,
} B2fKeyboard;

#define B2F_PROMPT_OPTIONS_MAX     8
#define B2F_PROMPT_OPTION_LENGTH   32 // Including the NUL, longer options are cut
#define B2F_PROMPT_QUESTION_LENGTH 128 // Including the NUL, lines end with '\n'

/**
 * A prompt the host asked, parsed from .B2F/prompt/
 */
typedef struct {
    uint32_t id; // Changes with every prompt the host asks
    B2fPromptType type;
    B2fKeyboard keyboard;
    char question[B2F_PROMPT_QUESTION_LENGTH];
    // Confirm: [0] right and [1] left button. Text: [0] is the default.
    char options[B2F_PROMPT_OPTIONS_MAX][B2F_PROMPT_OPTION_LENGTH];
    uint8_t option_count;
    uint32_t timeout; // Seconds, 0 waits forever
} B2fPrompt;

typedef enum {
    B2fAnswerOk,
    B2fAnswerCancel,
    B2fAnswerTimeout,
} B2fAnswer;

/**
 * Callback function type for .B2F changes, called from the USB thread
 * @param context User context pointer
 */
typedef void (*B2fDirUpdateCallback)(void* context);

B2fDir* b2f_dir_alloc(void);

void b2f_dir_free(B2fDir* b2f);

/**
 * Add the .B2F directory and files to a virtual FAT
 * Call before adding other files, they are then placed after the reserved area.
 * @return true on success
 */
bool b2f_dir_add_to(B2fDir* b2f, VirtualFat* vfat);

void b2f_dir_set_update_callback(B2fDir* b2f, B2fDirUpdateCallback callback, void* context);

/**
 * Copy the status text, printable ASCII and newlines only
 * @param text Output buffer
 * @param size Output buffer size
 * @return Text length, 0 if the host didn't set a status
 */
size_t b2f_dir_get_status(B2fDir* b2f, char* text, size_t size);

/**
 * What the host asked the Flipper to show
 */
B2fMode b2f_dir_get_mode(B2fDir* b2f);

/**
 * Get the prompt waiting for the user's answer
 * @param prompt Output, filled in if a prompt is waiting
 * @return true if a prompt is waiting
 */
bool b2f_dir_get_prompt(B2fDir* b2f, B2fPrompt* prompt);

/**
 * Answer the waiting prompt and go back to status mode
 * @param result How the prompt ended
 * @param value The chosen option or entered text, for B2fAnswerOk
 */
void b2f_dir_answer_prompt(B2fDir* b2f, B2fAnswer result, const char* value);

/**
 * Lock the frame against host writes while drawing it
 * @return Frame (B2F_FRAME_SIZE bytes), release it with b2f_dir_release_frame
 */
const uint8_t* b2f_dir_acquire_frame(B2fDir* b2f);

void b2f_dir_release_frame(B2fDir* b2f);
