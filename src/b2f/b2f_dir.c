#include "b2f_dir.h"
#include "b2f_efi_image.h"
#include "b2f_pxe_image.h"
#include "../log.h"

#define TAG "B2fDir"

#define B2F_ANSWER_PENDING "pending\n"
#define B2F_ANSWER_OK      "ok\n"
#define B2F_ANSWER_CANCEL  "cancel\n"
#define B2F_ANSWER_TIMEOUT "timeout\n"

typedef struct {
    B2fDir* b2f;
    uint8_t* host; // Buffer of the virtual FAT file, written by the USB thread
    uint8_t* shown; // Copy read by the GUI under b2f->mutex, or NULL
} B2fFile;

struct B2fDir {
    FuriMutex* mutex;
    B2fDirUpdateCallback callback;
    void* callback_context;

    uint8_t info[SECTOR_SIZE];
    uint8_t status_host[B2F_STATUS_SIZE];
    uint8_t status_shown[B2F_STATUS_SIZE];
    uint8_t frame_host[B2F_FRAME_SIZE];
    uint8_t frame_shown[B2F_FRAME_SIZE];
    uint8_t mode_host[B2F_MODE_SIZE]; // Also written by the Flipper after an answer

    // .B2F/prompt/, read by the Flipper when the prompt starts
    uint8_t prompt_type[B2F_PROMPT_FILES_SIZE];
    uint8_t question[B2F_QUESTION_SIZE];
    uint8_t options[B2F_OPTIONS_SIZE];
    uint8_t kbdtype[B2F_PROMPT_FILES_SIZE];
    uint8_t timeout[B2F_PROMPT_FILES_SIZE];
    uint8_t answer[B2F_PROMPT_FILES_SIZE]; // Written by the Flipper, read by the USB thread

    B2fMode mode;
    bool prompt_waiting;
    uint32_t prompt_count;
    B2fPrompt prompt;

    B2fFile status_file;
    B2fFile frame_file;
    B2fFile mode_file;
    B2fFile prompt_file;
};

// Writers for iPXE, from .B2F/bin up to the end of the area
_Static_assert(
    (sizeof(b2f_efi_image) + SECTOR_SIZE - 1) / SECTOR_SIZE +
            (sizeof(b2f_pxe_image) + SECTOR_SIZE - 1) / SECTOR_SIZE <
        B2F_AREA_END_LBA - B2F_BIN_LBA,
    ".B2F/bin doesn't fit the reserved area");

static void put_u16(uint8_t* p, uint16_t value) {
    p[0] = value & 0xFF;
    p[1] = value >> 8;
}

static void put_u32(uint8_t* p, uint32_t value) {
    put_u16(p, value & 0xFFFF);
    put_u16(p + 2, value >> 16);
}

static void b2f_dir_add_info_entry(
    uint8_t* info,
    const char* name,
    uint32_t lba,
    uint32_t size,
    uint32_t flags) {
    uint16_t count = info[10] | info[11] << 8;
    uint8_t* entry = info + 16 + count * 32;
    strncpy((char*)entry, name, 16);
    put_u32(entry + 16, lba);
    put_u32(entry + 20, size);
    put_u32(entry + 24, flags);
    put_u16(info + 10, count + 1);
}

B2fDir* b2f_dir_alloc(void) {
    B2fDir* b2f = malloc(sizeof(B2fDir));
    memset(b2f, 0, sizeof(B2fDir));
    b2f->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    b2f->status_file = (B2fFile){b2f, b2f->status_host, b2f->status_shown};
    b2f->frame_file = (B2fFile){b2f, b2f->frame_host, b2f->frame_shown};
    b2f->mode_file = (B2fFile){b2f, b2f->mode_host, NULL};
    b2f->prompt_file = (B2fFile){b2f, NULL, NULL};
    b2f->mode = B2fModeStatus;

    memcpy(b2f->info, B2F_MAGIC, 8);
    put_u16(b2f->info + 8, B2F_INFO_VERSION);
    return b2f;
}

void b2f_dir_free(B2fDir* b2f) {
    if(b2f == NULL) return;
    furi_mutex_free(b2f->mutex);
    free(b2f);
}

// Replace a Flipper-written file. The USB thread reads it without the mutex,
// so it must not see a half written file.
static void b2f_dir_set_file(uint8_t* buffer, size_t size, const char* text, const char* value) {
    FURI_CRITICAL_ENTER();
    memset(buffer, 0, size);
    size_t length = strlen(text);
    memcpy(buffer, text, length);
    if(value) {
        for(size_t i = 0; value[i] && length + 2 < size; i++) {
            buffer[length++] = value[i];
        }
        buffer[length] = '\n';
    }
    FURI_CRITICAL_EXIT();
}

// Length of the word at the start of text, up to a NUL, space or newline
static size_t b2f_dir_word_length(const uint8_t* text, size_t size) {
    size_t length = 0;
    while(length < size && text[length] > ' ')
        length++;
    return length;
}

static bool b2f_dir_word_is(const uint8_t* text, size_t size, const char* word) {
    size_t length = b2f_dir_word_length(text, size);
    return length == strlen(word) && memcmp(text, word, length) == 0;
}

// Copy the line at *pos of text into out (printable ASCII only), advance *pos
// past it. False if there are no more lines.
static bool
    b2f_dir_next_line(const uint8_t* text, size_t size, size_t* pos, char* out, size_t out_size) {
    if(*pos >= size || text[*pos] == '\0') return false;

    size_t length = 0;
    while(*pos < size && text[*pos] != '\0' && text[*pos] != '\n') {
        char ch = (char)text[(*pos)++];
        if(ch >= ' ' && ch <= '~' && length + 1 < out_size) out[length++] = ch;
    }
    out[length] = '\0';
    if(*pos < size && text[*pos] == '\n') (*pos)++;
    return true;
}

// Read the prompt files, called with the mutex held when the prompt starts
static void b2f_dir_parse_prompt(B2fDir* b2f) {
    B2fPrompt* prompt = &b2f->prompt;
    memset(prompt, 0, sizeof(B2fPrompt));
    prompt->id = ++b2f->prompt_count;

    if(b2f_dir_word_is(b2f->prompt_type, B2F_PROMPT_FILES_SIZE, "text")) {
        prompt->type = B2fPromptText;
    } else if(b2f_dir_word_is(b2f->prompt_type, B2F_PROMPT_FILES_SIZE, "confirm")) {
        prompt->type = B2fPromptConfirm;
    } else {
        prompt->type = B2fPromptChoice;
    }
    if(b2f_dir_word_is(b2f->kbdtype, B2F_PROMPT_FILES_SIZE, "ip")) {
        prompt->keyboard = B2fKeyboardIp;
    } else if(b2f_dir_word_is(b2f->kbdtype, B2F_PROMPT_FILES_SIZE, "number")) {
        prompt->keyboard = B2fKeyboardNumber;
    } else {
        prompt->keyboard = B2fKeyboardText;
    }

    // Seconds, up to B2F_PROMPT_TIMEOUT_MAX
    for(size_t i = 0;
        i < B2F_PROMPT_FILES_SIZE && b2f->timeout[i] >= '0' && b2f->timeout[i] <= '9';
        i++) {
        prompt->timeout = prompt->timeout * 10 + (b2f->timeout[i] - '0');
        if(prompt->timeout > B2F_PROMPT_TIMEOUT_MAX) prompt->timeout = B2F_PROMPT_TIMEOUT_MAX;
    }

    // The whole question, lines kept
    size_t length = 0;
    for(size_t i = 0; i < B2F_QUESTION_SIZE && b2f->question[i] != '\0'; i++) {
        char ch = (char)b2f->question[i];
        if((ch == '\n' || (ch >= ' ' && ch <= '~')) && length + 1 < sizeof(prompt->question)) {
            prompt->question[length++] = ch;
        }
    }
    while(length > 0 && prompt->question[length - 1] == '\n')
        length--;
    prompt->question[length] = '\0';

    // A choice skips empty lines, the text default may be empty
    size_t pos = 0;
    while(prompt->option_count < B2F_PROMPT_OPTIONS_MAX &&
          b2f_dir_next_line(
              b2f->options,
              B2F_OPTIONS_SIZE,
              &pos,
              prompt->options[prompt->option_count],
              B2F_PROMPT_OPTION_LENGTH)) {
        if(prompt->type == B2fPromptText) {
            prompt->option_count = 1;
            break;
        }
        if(prompt->options[prompt->option_count][0] != '\0') prompt->option_count++;
    }

    // Without options, a choice or confirm is Yes/No
    if(prompt->type != B2fPromptText && prompt->option_count == 0) {
        prompt->type = B2fPromptConfirm;
        strcpy(prompt->options[0], "Yes");
        strcpy(prompt->options[1], "No");
        prompt->option_count = 2;
    }
    if(prompt->type == B2fPromptConfirm && prompt->option_count > 2) prompt->option_count = 2;
}

// The host wrote .B2F/mode, called with the mutex held
static void b2f_dir_mode_written(B2fDir* b2f) {
    if(b2f_dir_word_is(b2f->mode_host, B2F_MODE_SIZE, B2F_MODE_FRAMEBUFFER)) {
        b2f->mode = B2fModeFramebuffer;
    } else if(b2f_dir_word_is(b2f->mode_host, B2F_MODE_SIZE, B2F_MODE_PROMPT)) {
        // Every write of "prompt" asks again
        b2f->mode = B2fModePrompt;
        b2f_dir_parse_prompt(b2f);
        b2f->prompt_waiting = true;
        b2f_dir_set_file(b2f->answer, B2F_PROMPT_FILES_SIZE, B2F_ANSWER_PENDING, NULL);
    } else {
        b2f->mode = B2fModeStatus;
    }
}

// The virtual FAT wrote to a host buffer
static void b2f_dir_file_written(void* context, uint32_t offset, uint32_t size) {
    B2fFile* file = context;
    B2fDir* b2f = file->b2f;

    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    if(file->shown) memcpy(file->shown + offset, file->host + offset, size);
    if(file == &b2f->mode_file) b2f_dir_mode_written(b2f);
    furi_mutex_release(b2f->mutex);

    if(b2f->callback) b2f->callback(b2f->callback_context);
}

typedef struct {
    const char* dir;
    const char* name; // Also the info entry name, after "prompt/" for the prompt
    uint32_t lba;
    uint8_t* buffer;
    uint32_t size;
    B2fFile* file; // NULL for read-only files
} B2fFileSpec;

bool b2f_dir_add_to(B2fDir* b2f, VirtualFat* vfat) {
    if(b2f == NULL || vfat == NULL) return false;

    const B2fFileSpec files[] = {
        {B2F_DIR_PATH,
         "status",
         B2F_STATUS_LBA,
         b2f->status_host,
         B2F_STATUS_SIZE,
         &b2f->status_file},
        {B2F_DIR_PATH, "frame", B2F_FRAME_LBA, b2f->frame_host, B2F_FRAME_SIZE, &b2f->frame_file},
        {B2F_DIR_PATH, "mode", B2F_MODE_LBA, b2f->mode_host, B2F_MODE_SIZE, &b2f->mode_file},
        {B2F_PROMPT_PATH,
         "type",
         B2F_PROMPT_TYPE_LBA,
         b2f->prompt_type,
         B2F_PROMPT_FILES_SIZE,
         &b2f->prompt_file},
        {B2F_PROMPT_PATH,
         "question",
         B2F_QUESTION_LBA,
         b2f->question,
         B2F_QUESTION_SIZE,
         &b2f->prompt_file},
        {B2F_PROMPT_PATH,
         "options",
         B2F_OPTIONS_LBA,
         b2f->options,
         B2F_OPTIONS_SIZE,
         &b2f->prompt_file},
        {B2F_PROMPT_PATH,
         "kbdtype",
         B2F_KBDTYPE_LBA,
         b2f->kbdtype,
         B2F_PROMPT_FILES_SIZE,
         &b2f->prompt_file},
        {B2F_PROMPT_PATH, "answer", B2F_ANSWER_LBA, b2f->answer, B2F_PROMPT_FILES_SIZE, NULL},
        {B2F_PROMPT_PATH,
         "timeout",
         B2F_TIMEOUT_LBA,
         b2f->timeout,
         B2F_PROMPT_FILES_SIZE,
         &b2f->prompt_file},
    };

    // The header lists every file
    put_u16(b2f->info + 10, 0);
    for(size_t i = 0; i < COUNT_OF(files); i++) {
        char name[16];
        snprintf(
            name,
            sizeof(name),
            "%s%s",
            strcmp(files[i].dir, B2F_PROMPT_PATH) == 0 ? "prompt/" : "",
            files[i].name);
        b2f_dir_add_info_entry(
            b2f->info, name, files[i].lba, files[i].size, files[i].file ? B2F_INFO_WRITABLE : 0);
    }
    // Fixed directories first, so the files' parents are not placed elsewhere
    bool ok =
        virtual_fat_add_fixed_directory(
            vfat, B2F_DIR_PATH, LBA_TO_CLUSTER(B2F_DIR_LBA), FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM) &&
        virtual_fat_add_fixed_directory(
            vfat, B2F_PROMPT_PATH, LBA_TO_CLUSTER(B2F_PROMPT_LBA), 0) &&
        virtual_fat_add_fixed_file(
            vfat,
            B2F_DIR_PATH,
            "info",
            LBA_TO_CLUSTER(B2F_INFO_LBA),
            b2f->info,
            SECTOR_SIZE,
            NULL,
            NULL);
    for(size_t i = 0; ok && i < COUNT_OF(files); i++) {
        ok = virtual_fat_add_fixed_file(
            vfat,
            files[i].dir,
            files[i].name,
            LBA_TO_CLUSTER(files[i].lba),
            files[i].buffer,
            files[i].size,
            files[i].file ? b2f_dir_file_written : NULL,
            files[i].file);
    }
    if(!ok) {
        B2F_LOG_E(TAG, "Failed to add " B2F_DIR_PATH);
        return false;
    }

    // Writers for iPXE, from .B2F/bin, so more fixed files fit before it
    if(!virtual_fat_add_fixed_directory(vfat, B2F_BIN_PATH, LBA_TO_CLUSTER(B2F_BIN_LBA), 0)) {
        B2F_LOG_E(TAG, "Failed to add " B2F_BIN_PATH);
        return false;
    }
    virtual_fat_set_allocation_start(vfat, LBA_TO_CLUSTER(B2F_BIN_LBA) + 1);
    if(!virtual_fat_add_static_file(
           vfat, B2F_BIN_PATH, "b2f.efi", b2f_efi_image, sizeof(b2f_efi_image)) ||
       !virtual_fat_add_static_file(
           vfat, B2F_BIN_PATH, "b2f.pxe", b2f_pxe_image, sizeof(b2f_pxe_image))) {
        B2F_LOG_E(TAG, "Failed to add " B2F_BIN_PATH);
        return false;
    }

    // Other files go after the reserved area
    virtual_fat_set_allocation_start(vfat, LBA_TO_CLUSTER(B2F_AREA_END_LBA));
    return true;
}

void b2f_dir_set_update_callback(B2fDir* b2f, B2fDirUpdateCallback callback, void* context) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    b2f->callback = callback;
    b2f->callback_context = context;
    furi_mutex_release(b2f->mutex);
}

size_t b2f_dir_get_status(B2fDir* b2f, char* text, size_t size) {
    if(size == 0) return 0;
    size_t length = 0;

    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    for(size_t i = 0; i < B2F_STATUS_SIZE && length + 1 < size; i++) {
        char ch = (char)b2f->status_shown[i];
        if(ch == '\0') break;
        if(ch == '\n' || (ch >= ' ' && ch <= '~')) text[length++] = ch; // Drops '\r' etc.
    }
    furi_mutex_release(b2f->mutex);

    // Trailing newlines don't make a line
    while(length > 0 && text[length - 1] == '\n')
        length--;
    text[length] = '\0';
    return length;
}

B2fMode b2f_dir_get_mode(B2fDir* b2f) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    B2fMode mode = b2f->mode;
    furi_mutex_release(b2f->mutex);
    return mode;
}

bool b2f_dir_get_prompt(B2fDir* b2f, B2fPrompt* prompt) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    bool waiting = b2f->prompt_waiting;
    if(waiting) *prompt = b2f->prompt;
    furi_mutex_release(b2f->mutex);
    return waiting;
}

void b2f_dir_answer_prompt(B2fDir* b2f, B2fAnswer result, const char* value) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    if(b2f->prompt_waiting) {
        b2f->prompt_waiting = false;
        if(result == B2fAnswerOk) {
            b2f_dir_set_file(b2f->answer, B2F_PROMPT_FILES_SIZE, B2F_ANSWER_OK, value);
        } else if(result == B2fAnswerTimeout) {
            b2f_dir_set_file(b2f->answer, B2F_PROMPT_FILES_SIZE, B2F_ANSWER_TIMEOUT, NULL);
        } else {
            b2f_dir_set_file(b2f->answer, B2F_PROMPT_FILES_SIZE, B2F_ANSWER_CANCEL, NULL);
        }

        // Back to status, and tell the host
        b2f->mode = B2fModeStatus;
        b2f_dir_set_file(b2f->mode_host, B2F_MODE_SIZE, B2F_MODE_STATUS, NULL);
    }
    furi_mutex_release(b2f->mutex);
}

const uint8_t* b2f_dir_acquire_frame(B2fDir* b2f) {
    furi_mutex_acquire(b2f->mutex, FuriWaitForever);
    return b2f->frame_shown;
}

void b2f_dir_release_frame(B2fDir* b2f) {
    furi_mutex_release(b2f->mutex);
}
