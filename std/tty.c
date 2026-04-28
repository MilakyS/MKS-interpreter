#define _XOPEN_SOURCE 700

#include "tty.h"

#include "../GC/gc.h"
#include "../Runtime/module.h"
#include "../Runtime/errors.h"
#include "../Runtime/context.h"
#include "../Runtime/value.h"
#include "../Utils/hash.h"
#include "../env/env.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#define TTY_ESC_CLEAR_SCREEN  "\x1b[2J\x1b[H"
#define TTY_ESC_CLEAR_LINE    "\x1b[2K\r"
#define TTY_ESC_HIDE_CURSOR   "\x1b[?25l"
#define TTY_ESC_SHOW_CURSOR   "\x1b[?25h"
#define TTY_ESC_ALT_SCREEN    "\x1b[?1049h"
#define TTY_ESC_MAIN_SCREEN   "\x1b[?1049l"
#define TTY_ESC_RESET         "\x1b[0m"

#define TTY_BORDER_SINGLE 0
#define TTY_BORDER_DOUBLE 1
#define TTY_BORDER_ROUND  2
#define TTY_BORDER_HEAVY  3

#define TTY_ALIGN_LEFT   0
#define TTY_ALIGN_CENTER 1
#define TTY_ALIGN_RIGHT  2

typedef struct TTYState {
    struct termios original;
    int raw_enabled;
    int cleanup_registered;
    int alt_screen_enabled;
    int cursor_hidden;
} TTYState;

typedef struct TTYBorderStyle {
    const char *tl;
    const char *tr;
    const char *bl;
    const char *br;
    const char *h;
    const char *v;
} TTYBorderStyle;

static TTYState tty_state = {0};

static int tty_is_string(RuntimeValue value) {
    return value.type == VAL_STRING;
}

static int tty_is_number(RuntimeValue value) {
    return value.type == VAL_INT || value.type == VAL_FLOAT;
}

static long long tty_as_number(RuntimeValue value) {
    if (value.type == VAL_FLOAT) {
        return (long long)value.data.float_value;
    }

    return (long long)value.data.int_value;
}

static const char *tty_as_string(RuntimeValue value, size_t *out_len) {
    if (value.data.managed_string == NULL) {
        if (out_len != NULL) {
            *out_len = 0;
        }
        return "";
    }

    if (out_len != NULL) {
        *out_len = value.data.managed_string->len;
    }

    return value.data.managed_string->data;
}

static void tty_write_ansi(const char *text) {
    fputs(text, stdout);
}

static void tty_flush_stdout(void) {
    fflush(stdout);
}

static void tty_goto(long long x, long long y) {
    if (x < 1) {
        x = 1;
    }

    if (y < 1) {
        y = 1;
    }

    printf("\x1b[%lld;%lldH", y, x);
}

static void tty_repeat(const char *text, long long count) {
    if (text == NULL || count <= 0) {
        return;
    }

    for (long long i = 0; i < count; i++) {
        fputs(text, stdout);
    }
}

static size_t tty_utf8_char_len(unsigned char c) {
    if (c < 0x80) {
        return 1;
    }

    if ((c & 0xe0) == 0xc0) {
        return 2;
    }

    if ((c & 0xf0) == 0xe0) {
        return 3;
    }

    if ((c & 0xf8) == 0xf0) {
        return 4;
    }

    return 1;
}

static uint32_t tty_decode_utf8(const char *text, size_t len, size_t *used) {
    if (len == 0) {
        *used = 0;
        return 0;
    }

    const unsigned char *s = (const unsigned char *)text;
    size_t n = tty_utf8_char_len(s[0]);

    if (n > len) {
        *used = 1;
        return s[0];
    }

    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            *used = 1;
            return s[0];
        }
    }

    *used = n;

    if (n == 1) {
        return s[0];
    }

    if (n == 2) {
        return ((uint32_t)(s[0] & 0x1f) << 6) |
               (uint32_t)(s[1] & 0x3f);
    }

    if (n == 3) {
        return ((uint32_t)(s[0] & 0x0f) << 12) |
               ((uint32_t)(s[1] & 0x3f) << 6) |
               (uint32_t)(s[2] & 0x3f);
    }

    return ((uint32_t)(s[0] & 0x07) << 18) |
           ((uint32_t)(s[1] & 0x3f) << 12) |
           ((uint32_t)(s[2] & 0x3f) << 6) |
           (uint32_t)(s[3] & 0x3f);
}

static int tty_codepoint_width(uint32_t cp) {
    if (cp == 0 || cp == '\n' || cp == '\r' || cp == '\t') {
        return cp == '\t' ? 4 : 0;
    }

    if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) {
        return 0;
    }

    if ((cp >= 0x0300 && cp <= 0x036f) ||
        (cp >= 0x1ab0 && cp <= 0x1aff) ||
        (cp >= 0x1dc0 && cp <= 0x1dff) ||
        (cp >= 0x20d0 && cp <= 0x20ff) ||
        (cp >= 0xfe20 && cp <= 0xfe2f)) {
        return 0;
    }

    if ((cp >= 0x1100 && cp <= 0x115f) ||
        (cp >= 0x2e80 && cp <= 0xa4cf) ||
        (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe10 && cp <= 0xfe19) ||
        (cp >= 0xfe30 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6)) {
        return 2;
    }

    return 1;
}

static long long tty_display_width(const char *text, size_t len) {
    long long width = 0;
    size_t offset = 0;

    while (offset < len) {
        size_t used = 0;
        uint32_t cp = tty_decode_utf8(text + offset, len - offset, &used);
        if (used == 0) {
            break;
        }

        width += tty_codepoint_width(cp);
        offset += used;
    }

    return width;
}

static size_t tty_prefix_bytes_for_columns(const char *text,
                                           size_t len,
                                           long long max,
                                           long long *out_width) {
    size_t offset = 0;
    long long width = 0;

    while (offset < len && width < max) {
        size_t used = 0;
        uint32_t cp = tty_decode_utf8(text + offset, len - offset, &used);
        if (used == 0) {
            break;
        }

        int char_width = tty_codepoint_width(cp);
        if (width + char_width > max) {
            break;
        }

        width += char_width;
        offset += used;
    }

    if (out_width != NULL) {
        *out_width = width;
    }

    return offset;
}

static void tty_write_clip(const char *text, size_t len, long long max) {
    if (text == NULL || len == 0 || max <= 0) {
        return;
    }

    size_t n = tty_prefix_bytes_for_columns(text, len, max, NULL);

    fwrite(text, 1, n, stdout);
}

static size_t tty_byte_offset_for_columns(const char *text, size_t len, long long columns) {
    size_t offset = 0;
    long long width = 0;

    if (columns <= 0) {
        return 0;
    }

    while (offset < len && width < columns) {
        size_t used = 0;
        uint32_t cp = tty_decode_utf8(text + offset, len - offset, &used);
        if (used == 0) {
            break;
        }

        int char_width = tty_codepoint_width(cp);
        if (width + char_width > columns) {
            break;
        }

        width += char_width;
        offset += used;
    }

    return offset;
}

static int tty_is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

static void tty_write_wrapped(const char *text,
                              size_t len,
                              long long x,
                              long long y,
                              long long w,
                              long long h,
                              int clear_lines) {
    if (text == NULL || w <= 0 || h <= 0) {
        return;
    }

    size_t offset = 0;

    for (long long row = 0; row < h; row++) {
        if (clear_lines) {
            tty_goto(x, y + row);
            tty_repeat(" ", w);
        }

        while (offset < len) {
            size_t used = 0;
            uint32_t cp = tty_decode_utf8(text + offset, len - offset, &used);
            if (used == 0 || !tty_is_space(cp) || cp == '\n' || cp == '\r') {
                break;
            }
            offset += used;
        }

        if (offset >= len) {
            continue;
        }

        size_t start = offset;
        size_t scan = offset;
        size_t last_space = 0;
        long long line_width = 0;

        while (scan < len) {
            size_t used = 0;
            uint32_t cp = tty_decode_utf8(text + scan, len - scan, &used);
            if (used == 0) {
                break;
            }

            if (cp == '\n' || cp == '\r') {
                break;
            }

            int char_width = tty_codepoint_width(cp);
            if (line_width + char_width > w) {
                break;
            }

            if (tty_is_space(cp)) {
                last_space = scan;
            }

            line_width += char_width;
            scan += used;
        }

        size_t end = scan;
        int broke_on_newline = 0;

        if (scan < len) {
            size_t used = 0;
            uint32_t cp = tty_decode_utf8(text + scan, len - scan, &used);
            broke_on_newline = cp == '\n' || cp == '\r';
        }

        if (!broke_on_newline && scan < len && last_space > start) {
            end = last_space;
            offset = last_space;
        } else {
            offset = scan;
        }

        tty_goto(x, y + row);
        tty_write_clip(text + start, end - start, w);

        while (offset < len) {
            size_t used = 0;
            uint32_t cp = tty_decode_utf8(text + offset, len - offset, &used);
            if (used == 0) {
                break;
            }

            if (cp == '\n' || cp == '\r') {
                offset += used;
                break;
            }

            if (!tty_is_space(cp)) {
                break;
            }

            offset += used;
        }
    }
}

static TTYBorderStyle tty_border_style(int style) {
    switch (style) {
        case TTY_BORDER_DOUBLE:
            return (TTYBorderStyle){"╔", "╗", "╚", "╝", "═", "║"};

        case TTY_BORDER_ROUND:
            return (TTYBorderStyle){"╭", "╮", "╰", "╯", "─", "│"};

        case TTY_BORDER_HEAVY:
            return (TTYBorderStyle){"┏", "┓", "┗", "┛", "━", "┃"};

        case TTY_BORDER_SINGLE:
        default:
            return (TTYBorderStyle){"┌", "┐", "└", "┘", "─", "│"};
    }
}

static void tty_require_arg_count(const char *name, int got, int expected) {
    if (got != expected) {
        runtime_error("tty.%s expects %d argument%s",
                      name,
                      expected,
                      expected == 1 ? "" : "s");
    }
}

static void tty_require_number(const char *name, const RuntimeValue *args, int index) {
    if (!tty_is_number(args[index])) {
        runtime_error("tty.%s: argument %d must be number", name, index + 1);
    }
}

static void tty_require_string(const char *name, const RuntimeValue *args, int index) {
    if (!tty_is_string(args[index])) {
        runtime_error("tty.%s: argument %d must be string", name, index + 1);
    }
}

static void tty_restore(void) {
    if (tty_state.cursor_hidden) {
        tty_write_ansi(TTY_ESC_SHOW_CURSOR);
        tty_state.cursor_hidden = 0;
    }

    if (tty_state.alt_screen_enabled) {
        tty_write_ansi(TTY_ESC_MAIN_SCREEN);
        tty_state.alt_screen_enabled = 0;
    }

    tty_write_ansi(TTY_ESC_RESET);
    tty_flush_stdout();

    if (tty_state.raw_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &tty_state.original);
        tty_state.raw_enabled = 0;
    }
}

static void tty_register_cleanup_once(void) {
    if (!tty_state.cleanup_registered) {
        atexit(tty_restore);
        tty_state.cleanup_registered = 1;
    }
}

static int tty_set_raw_mode(int enable) {
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }

    tty_register_cleanup_once();

    if (enable && !tty_state.raw_enabled) {
        struct termios raw;

        if (tcgetattr(STDIN_FILENO, &tty_state.original) != 0) {
            return 0;
        }

        raw = tty_state.original;

        raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= (tcflag_t)~(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);

        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return 0;
        }

        tty_state.raw_enabled = 1;
        return 1;
    }

    if (!enable && tty_state.raw_enabled) {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tty_state.original) != 0) {
            return 0;
        }

        tty_state.raw_enabled = 0;
        return 1;
    }

    return 1;
}

static int tty_read_byte(char *out, int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    struct timeval tv;
    struct timeval *tv_ptr = NULL;

    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tv_ptr = &tv;
    }

    int rv = select(STDIN_FILENO + 1, &rfds, NULL, NULL, tv_ptr);
    if (rv <= 0) {
        return 0;
    }

    ssize_t n = read(STDIN_FILENO, out, 1);
    return n == 1;
}

static RuntimeValue tty_make_size_object(unsigned short width, unsigned short height) {
    Environment *obj_env = env_create_child(NULL);
    gc_push_env(obj_env);

    RuntimeValue obj = make_object(obj_env);
    gc_push_root(&obj);

    env_set_fast(obj_env, "w", get_hash("w"), make_int(width));
    env_set_fast(obj_env, "h", get_hash("h"), make_int(height));

    gc_pop_root();
    gc_pop_env();

    return obj;
}

static RuntimeValue tty_make_edit_object(const char *value, long long cursor) {
    Environment *obj_env = env_create_child(NULL);
    gc_push_env(obj_env);

    RuntimeValue obj = make_object(obj_env);
    gc_push_root(&obj);

    env_set_fast(obj_env, "value", get_hash("value"), make_string(value != NULL ? value : ""));
    env_set_fast(obj_env, "cursor", get_hash("cursor"), make_int(cursor));

    gc_pop_root();
    gc_pop_env();

    return obj;
}

static size_t tty_byte_offset_for_char_index(const char *text, size_t len, long long index) {
    size_t offset = 0;
    long long current = 0;

    if (index <= 0) {
        return 0;
    }

    while (offset < len && current < index) {
        size_t used = 0;
        tty_decode_utf8(text + offset, len - offset, &used);
        if (used == 0) {
            break;
        }
        offset += used;
        current++;
    }

    return offset;
}

static long long tty_char_count(const char *text, size_t len) {
    size_t offset = 0;
    long long count = 0;

    while (offset < len) {
        size_t used = 0;
        tty_decode_utf8(text + offset, len - offset, &used);
        if (used == 0) {
            break;
        }
        offset += used;
        count++;
    }

    return count;
}

static RuntimeValue tty_parse_escape_sequence(void) {
    char seq1;
    char seq2;

    if (!tty_read_byte(&seq1, 20)) {
        return make_string("esc");
    }

    if (!tty_read_byte(&seq2, 20)) {
        return make_string("esc");
    }

    if (seq1 == '[') {
        switch (seq2) {
            case 'A': return make_string("up");
            case 'B': return make_string("down");
            case 'C': return make_string("right");
            case 'D': return make_string("left");
            case 'H': return make_string("home");
            case 'F': return make_string("end");
            default: break;
        }
    }

    if (seq1 == 'O') {
        switch (seq2) {
            case 'H': return make_string("home");
            case 'F': return make_string("end");
            default: break;
        }
    }

    return make_string("esc");
}

static RuntimeValue tty_read_key_with_timeout(int timeout_ms) {
    char c;
    if (!tty_read_byte(&c, timeout_ms)) {
        return make_null();
    }

    if (c == '\x1b') {
        return tty_parse_escape_sequence();
    }

    if (c == '\r' || c == '\n') {
        return make_string("enter");
    }

    if (c == 127 || c == '\b') {
        return make_string("backspace");
    }

    if ((unsigned char)c == 3) {
        return make_string("ctrl_c");
    }

    if ((unsigned char)c == 4) {
        return make_string("ctrl_d");
    }

    if ((unsigned char)c == 9) {
        return make_string("tab");
    }

    return make_string_len(&c, 1);
}

static RuntimeValue n_set_raw(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    int enable = 1;

    if (arg_count > 1) {
        runtime_error("tty.set_raw expects 0 or 1 arguments");
    }

    if (arg_count == 1) {
        tty_require_number("set_raw", args, 0);
        enable = (int)tty_as_number(args[0]);
    }

    tty_set_raw_mode(enable != 0);
    return make_null();
}

static RuntimeValue n_raw(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("raw", arg_count, 0);

    if (!tty_set_raw_mode(1)) {
        runtime_error("tty.raw: failed to enable raw mode");
    }

    return make_null();
}

static RuntimeValue n_cooked(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("cooked", arg_count, 0);

    if (!tty_set_raw_mode(0)) {
        runtime_error("tty.cooked: failed to restore terminal mode");
    }

    return make_null();
}

static RuntimeValue n_restore(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("restore", arg_count, 0);

    tty_restore();
    return make_null();
}

static RuntimeValue n_readch(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("readch", arg_count, 0);

    char c;
    if (!tty_read_byte(&c, 0)) {
        return make_null();
    }

    return make_string_len(&c, 1);
}

static RuntimeValue n_read_key(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("read_key", arg_count, 0);

    return tty_read_key_with_timeout(-1);
}

static RuntimeValue n_read_key_timeout(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("read_key_timeout", arg_count, 1);
    tty_require_number("read_key_timeout", args, 0);

    long long timeout_ms = tty_as_number(args[0]);
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }

    return tty_read_key_with_timeout((int)timeout_ms);
}

static RuntimeValue n_edit_text(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("edit_text", arg_count, 3);
    tty_require_string("edit_text", args, 0);
    tty_require_number("edit_text", args, 1);
    tty_require_string("edit_text", args, 2);

    size_t value_len = 0;
    const char *value = tty_as_string(args[0], &value_len);
    long long cursor = tty_as_number(args[1]);
    size_t key_len = 0;
    const char *key = tty_as_string(args[2], &key_len);

    long long char_count = tty_char_count(value, value_len);
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > char_count) {
        cursor = char_count;
    }

    if (strcmp(key, "left") == 0) {
        return tty_make_edit_object(value, cursor > 0 ? cursor - 1 : 0);
    }

    if (strcmp(key, "right") == 0) {
        return tty_make_edit_object(value, cursor < char_count ? cursor + 1 : char_count);
    }

    if (strcmp(key, "home") == 0) {
        return tty_make_edit_object(value, 0);
    }

    if (strcmp(key, "end") == 0) {
        return tty_make_edit_object(value, char_count);
    }

    if (strcmp(key, "backspace") == 0) {
        if (cursor <= 0) {
            return tty_make_edit_object(value, cursor);
        }

        size_t remove_start = tty_byte_offset_for_char_index(value, value_len, cursor - 1);
        size_t remove_end = tty_byte_offset_for_char_index(value, value_len, cursor);
        size_t new_len = value_len - (remove_end - remove_start);
        char *next = (char *)malloc(new_len + 1);
        if (next == NULL) {
            runtime_error("tty.edit_text: out of memory");
        }
        memcpy(next, value, remove_start);
        memcpy(next + remove_start, value + remove_end, value_len - remove_end);
        next[new_len] = '\0';
        RuntimeValue result = tty_make_edit_object(next, cursor - 1);
        free(next);
        return result;
    }

    if (key_len == 0 || key_len > 8 || strcmp(key, "enter") == 0 || strcmp(key, "tab") == 0) {
        return tty_make_edit_object(value, cursor);
    }

    size_t used = 0;
    uint32_t cp = tty_decode_utf8(key, key_len, &used);
    if (used != key_len || cp < 0x20 || cp == 0x7f) {
        return tty_make_edit_object(value, cursor);
    }

    size_t insert_at = tty_byte_offset_for_char_index(value, value_len, cursor);
    size_t new_len = value_len + key_len;
    char *next = (char *)malloc(new_len + 1);
    if (next == NULL) {
        runtime_error("tty.edit_text: out of memory");
    }
    memcpy(next, value, insert_at);
    memcpy(next + insert_at, key, key_len);
    memcpy(next + insert_at + key_len, value + insert_at, value_len - insert_at);
    next[new_len] = '\0';

    RuntimeValue result = tty_make_edit_object(next, cursor + 1);
    free(next);
    return result;
}

static RuntimeValue n_clear(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("clear", arg_count, 0);

    tty_write_ansi(TTY_ESC_CLEAR_SCREEN);
    return make_null();
}

static RuntimeValue n_clear_line(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("clear_line", arg_count, 0);

    tty_write_ansi(TTY_ESC_CLEAR_LINE);
    return make_null();
}

static RuntimeValue n_move(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("move", arg_count, 2);
    tty_require_number("move", args, 0);
    tty_require_number("move", args, 1);

    tty_goto(tty_as_number(args[0]), tty_as_number(args[1]));
    return make_null();
}

static RuntimeValue n_write(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("write", arg_count, 1);
    tty_require_string("write", args, 0);

    size_t len = 0;
    const char *text = tty_as_string(args[0], &len);

    if (len > 0) {
        fwrite(text, 1, len, stdout);
    }

    return make_null();
}

static RuntimeValue n_write_at(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("write_at", arg_count, 3);
    tty_require_number("write_at", args, 0);
    tty_require_number("write_at", args, 1);
    tty_require_string("write_at", args, 2);

    tty_goto(tty_as_number(args[0]), tty_as_number(args[1]));

    size_t len = 0;
    const char *text = tty_as_string(args[2], &len);

    if (len > 0) {
        fwrite(text, 1, len, stdout);
    }

    return make_null();
}

static RuntimeValue n_text_at(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("text_at", arg_count, 4);
    tty_require_number("text_at", args, 0);
    tty_require_number("text_at", args, 1);
    tty_require_string("text_at", args, 2);
    tty_require_number("text_at", args, 3);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long max = tty_as_number(args[3]);

    size_t len = 0;
    const char *text = tty_as_string(args[2], &len);

    tty_goto(x, y);
    tty_write_clip(text, len, max);

    return make_null();
}

static RuntimeValue n_text(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 6 && arg_count != 7) {
        runtime_error("tty.text expects 6 or 7 arguments");
    }

    tty_require_number("text", args, 0);
    tty_require_number("text", args, 1);
    tty_require_number("text", args, 2);
    tty_require_string("text", args, 3);
    tty_require_number("text", args, 4);
    tty_require_number("text", args, 5);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    const int align = (int)tty_as_number(args[4]);
    const int clear_line = (int)tty_as_number(args[5]);

    size_t len = 0;
    const char *text = tty_as_string(args[3], &len);

    long long visible = tty_display_width(text, len);
    if (visible > w) {
        visible = w;
    }

    long long start_x = x;

    if (align == TTY_ALIGN_CENTER) {
        start_x = x + ((w - visible) / 2);
    } else if (align == TTY_ALIGN_RIGHT) {
        start_x = x + (w - visible);
    }

    if (clear_line) {
        tty_goto(x, y);
        tty_repeat(" ", w);
    }

    tty_goto(start_x, y);
    tty_write_clip(text, len, w);

    return make_null();
}

static RuntimeValue n_text_wrap(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.text_wrap expects 5 or 6 arguments");
    }

    tty_require_number("text_wrap", args, 0);
    tty_require_number("text_wrap", args, 1);
    tty_require_number("text_wrap", args, 2);
    tty_require_number("text_wrap", args, 3);
    tty_require_string("text_wrap", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);
    int clear_lines = 1;

    if (arg_count == 6) {
        tty_require_number("text_wrap", args, 5);
        clear_lines = (int)tty_as_number(args[5]);
    }

    size_t len = 0;
    const char *text = tty_as_string(args[4], &len);

    tty_write_wrapped(text, len, x, y, w, h, clear_lines);
    return make_null();
}

static RuntimeValue n_flush(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("flush", arg_count, 0);

    tty_flush_stdout();
    return make_null();
}

static RuntimeValue n_hide_cursor(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("hide_cursor", arg_count, 0);

    tty_register_cleanup_once();
    tty_write_ansi(TTY_ESC_HIDE_CURSOR);
    tty_state.cursor_hidden = 1;
    return make_null();
}

static RuntimeValue n_show_cursor(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("show_cursor", arg_count, 0);

    tty_write_ansi(TTY_ESC_SHOW_CURSOR);
    tty_state.cursor_hidden = 0;
    return make_null();
}

static RuntimeValue n_alt_screen(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("alt_screen", arg_count, 0);

    tty_register_cleanup_once();
    tty_write_ansi(TTY_ESC_ALT_SCREEN);
    tty_state.alt_screen_enabled = 1;
    return make_null();
}

static RuntimeValue n_normal_screen(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("normal_screen", arg_count, 0);

    tty_write_ansi(TTY_ESC_MAIN_SCREEN);
    tty_state.alt_screen_enabled = 0;
    return make_null();
}

static RuntimeValue n_size(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("size", arg_count, 0);

    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        runtime_error("tty.size: ioctl failed: %s", strerror(errno));
    }

    return tty_make_size_object(ws.ws_col, ws.ws_row);
}

static RuntimeValue n_size_or(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("size_or", arg_count, 2);
    tty_require_number("size_or", args, 0);
    tty_require_number("size_or", args, 1);

    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return tty_make_size_object(ws.ws_col, ws.ws_row);
    }

    long long w = tty_as_number(args[0]);
    long long h = tty_as_number(args[1]);

    if (w < 1) {
        w = 80;
    }

    if (h < 1) {
        h = 24;
    }

    return tty_make_size_object((unsigned short)w, (unsigned short)h);
}

static RuntimeValue n_is_tty(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("is_tty", arg_count, 0);
    return make_bool(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO));
}

static RuntimeValue n_reset(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("reset", arg_count, 0);

    tty_write_ansi(TTY_ESC_RESET);
    return make_null();
}

static RuntimeValue n_fg(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("fg", arg_count, 1);
    tty_require_number("fg", args, 0);

    long long code = tty_as_number(args[0]);

    if (code < 0 || code > 255) {
        runtime_error("tty.fg: color code must be 0..255");
    }

    printf("\x1b[38;5;%lldm", code);
    return make_null();
}

static RuntimeValue n_bg(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("bg", arg_count, 1);
    tty_require_number("bg", args, 0);

    long long code = tty_as_number(args[0]);

    if (code < 0 || code > 255) {
        runtime_error("tty.bg: color code must be 0..255");
    }

    printf("\x1b[48;5;%lldm", code);
    return make_null();
}

static RuntimeValue n_style(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("style", arg_count, 1);
    tty_require_number("style", args, 0);

    long long code = tty_as_number(args[0]);

    if (code < 0 || code > 107) {
        runtime_error("tty.style: ansi code must be 0..107");
    }

    printf("\x1b[%lldm", code);
    return make_null();
}

static RuntimeValue n_bold(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("bold", arg_count, 0);

    tty_write_ansi("\x1b[1m");
    return make_null();
}

static RuntimeValue n_dim(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("dim", arg_count, 0);

    tty_write_ansi("\x1b[2m");
    return make_null();
}

static RuntimeValue n_underline(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("underline", arg_count, 0);

    tty_write_ansi("\x1b[4m");
    return make_null();
}

static RuntimeValue n_reverse(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("reverse", arg_count, 0);

    tty_write_ansi("\x1b[7m");
    return make_null();
}

static RuntimeValue n_rect(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("rect", arg_count, 5);
    tty_require_number("rect", args, 0);
    tty_require_number("rect", args, 1);
    tty_require_number("rect", args, 2);
    tty_require_number("rect", args, 3);
    tty_require_string("rect", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    size_t len = 0;
    const char *fill = tty_as_string(args[4], &len);

    if (w <= 0 || h <= 0 || len == 0) {
        return make_null();
    }

    for (long long row = 0; row < h; row++) {
        tty_goto(x, y + row);

        for (long long col = 0; col < w; col++) {
            fwrite(fill, 1, len, stdout);
        }
    }

    return make_null();
}

static RuntimeValue n_erase_rect(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("erase_rect", arg_count, 4);
    tty_require_number("erase_rect", args, 0);
    tty_require_number("erase_rect", args, 1);
    tty_require_number("erase_rect", args, 2);
    tty_require_number("erase_rect", args, 3);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    if (w <= 0 || h <= 0) {
        return make_null();
    }

    for (long long row = 0; row < h; row++) {
        tty_goto(x, y + row);
        tty_repeat(" ", w);
    }

    return make_null();
}

static RuntimeValue n_hline(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 3 && arg_count != 4) {
        runtime_error("tty.hline expects 3 or 4 arguments");
    }

    tty_require_number("hline", args, 0);
    tty_require_number("hline", args, 1);
    tty_require_number("hline", args, 2);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);

    const char *ch = "─";
    size_t len = 0;

    if (arg_count == 4) {
        tty_require_string("hline", args, 3);
        ch = tty_as_string(args[3], &len);
        if (len == 0) {
            ch = "─";
        }
    }

    tty_goto(x, y);
    tty_repeat(ch, w);

    return make_null();
}

static RuntimeValue n_vline(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 3 && arg_count != 4) {
        runtime_error("tty.vline expects 3 or 4 arguments");
    }

    tty_require_number("vline", args, 0);
    tty_require_number("vline", args, 1);
    tty_require_number("vline", args, 2);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long h = tty_as_number(args[2]);

    const char *ch = "│";
    size_t len = 0;

    if (arg_count == 4) {
        tty_require_string("vline", args, 3);
        ch = tty_as_string(args[3], &len);
        if (len == 0) {
            ch = "│";
        }
    }

    for (long long row = 0; row < h; row++) {
        tty_goto(x, y + row);
        fputs(ch, stdout);
    }

    return make_null();
}

static RuntimeValue n_box(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 4 && arg_count != 5) {
        runtime_error("tty.box expects 4 or 5 arguments");
    }

    tty_require_number("box", args, 0);
    tty_require_number("box", args, 1);
    tty_require_number("box", args, 2);
    tty_require_number("box", args, 3);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);
    int style = TTY_BORDER_SINGLE;

    if (arg_count == 5) {
        tty_require_number("box", args, 4);
        style = (int)tty_as_number(args[4]);
    }

    if (w < 2 || h < 2) {
        return make_null();
    }

    TTYBorderStyle bs = tty_border_style(style);

    tty_goto(x, y);
    fputs(bs.tl, stdout);
    tty_repeat(bs.h, w - 2);
    fputs(bs.tr, stdout);

    for (long long row = 1; row < h - 1; row++) {
        tty_goto(x, y + row);
        fputs(bs.v, stdout);

        tty_goto(x + w - 1, y + row);
        fputs(bs.v, stdout);
    }

    tty_goto(x, y + h - 1);
    fputs(bs.bl, stdout);
    tty_repeat(bs.h, w - 2);
    fputs(bs.br, stdout);

    return make_null();
}

static RuntimeValue n_box_fill(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.box_fill expects 5 or 6 arguments");
    }

    tty_require_number("box_fill", args, 0);
    tty_require_number("box_fill", args, 1);
    tty_require_number("box_fill", args, 2);
    tty_require_number("box_fill", args, 3);
    tty_require_string("box_fill", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    size_t fill_len = 0;
    const char *fill = tty_as_string(args[4], &fill_len);

    int style = TTY_BORDER_SINGLE;

    if (arg_count == 6) {
        tty_require_number("box_fill", args, 5);
        style = (int)tty_as_number(args[5]);
    }

    if (w < 2 || h < 2 || fill_len == 0) {
        return make_null();
    }

    for (long long row = 1; row < h - 1; row++) {
        tty_goto(x + 1, y + row);

        for (long long col = 0; col < w - 2; col++) {
            fwrite(fill, 1, fill_len, stdout);
        }
    }

    RuntimeValue box_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(h),
        make_int(style)
    };

    n_box(ctx, box_args, 5);

    return make_null();
}

static RuntimeValue n_title(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.title expects 5 or 6 arguments");
    }

    tty_require_number("title", args, 0);
    tty_require_number("title", args, 1);
    tty_require_number("title", args, 2);
    tty_require_string("title", args, 3);
    tty_require_number("title", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    int align = (int)tty_as_number(args[4]);

    size_t len = 0;
    const char *title = tty_as_string(args[3], &len);

    const char *left_pad = " ";
    const char *right_pad = " ";
    long long visible_len = tty_display_width(title, len);
    long long full_len = visible_len + 2;

    if (w <= 4 || len == 0) {
        return make_null();
    }

    long long max_title = w - 4;
    if (full_len > max_title) {
        full_len = max_title;
    }

    long long tx = x + 2;

    if (align == TTY_ALIGN_CENTER) {
        tx = x + ((w - full_len) / 2);
    } else if (align == TTY_ALIGN_RIGHT) {
        tx = x + w - full_len - 2;
    }

    tty_goto(tx, y);
    fputs(left_pad, stdout);
    tty_write_clip(title, len, full_len - 2);
    fputs(right_pad, stdout);

    return make_null();
}

static RuntimeValue n_window(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.window expects 5 or 6 arguments");
    }

    tty_require_number("window", args, 0);
    tty_require_number("window", args, 1);
    tty_require_number("window", args, 2);
    tty_require_number("window", args, 3);
    tty_require_string("window", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    int style = TTY_BORDER_ROUND;

    if (arg_count == 6) {
        tty_require_number("window", args, 5);
        style = (int)tty_as_number(args[5]);
    }

    if (w < 4 || h < 3) {
        return make_null();
    }

    RuntimeValue box_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(h),
        make_int(style)
    };

    n_box(ctx, box_args, 5);

    RuntimeValue title_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        args[4],
        make_int(TTY_ALIGN_LEFT)
    };

    n_title(ctx, title_args, 5);

    return make_null();
}

static RuntimeValue n_shadow(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 4 && arg_count != 5) {
        runtime_error("tty.shadow expects 4 or 5 arguments");
    }

    tty_require_number("shadow", args, 0);
    tty_require_number("shadow", args, 1);
    tty_require_number("shadow", args, 2);
    tty_require_number("shadow", args, 3);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    const char *ch = "░";
    size_t len = 0;

    if (arg_count == 5) {
        tty_require_string("shadow", args, 4);
        ch = tty_as_string(args[4], &len);
        if (len == 0) {
            ch = "░";
        }
    }

    if (w <= 0 || h <= 0) {
        return make_null();
    }

    tty_write_ansi("\x1b[90m");

    for (long long row = 1; row <= h; row++) {
        tty_goto(x + w, y + row);
        fputs(ch, stdout);
    }

    tty_goto(x + 1, y + h);
    tty_repeat(ch, w);

    tty_write_ansi(TTY_ESC_RESET);

    return make_null();
}

static RuntimeValue n_floating_window(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.floating_window expects 5 or 6 arguments");
    }

    tty_require_number("floating_window", args, 0);
    tty_require_number("floating_window", args, 1);
    tty_require_number("floating_window", args, 2);
    tty_require_number("floating_window", args, 3);
    tty_require_string("floating_window", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long h = tty_as_number(args[3]);

    int style = TTY_BORDER_ROUND;

    if (arg_count == 6) {
        tty_require_number("floating_window", args, 5);
        style = (int)tty_as_number(args[5]);
    }

    RuntimeValue shadow_args[4] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(h)
    };

    n_shadow(ctx, shadow_args, 4);

    RuntimeValue win_args[6] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(h),
        args[4],
        make_int(style)
    };

    n_window(ctx, win_args, 6);

    return make_null();
}

static RuntimeValue n_button(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5 && arg_count != 6) {
        runtime_error("tty.button expects 5 or 6 arguments");
    }

    tty_require_number("button", args, 0);
    tty_require_number("button", args, 1);
    tty_require_number("button", args, 2);
    tty_require_string("button", args, 3);
    tty_require_number("button", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    int active = (int)tty_as_number(args[4]);

    int style = TTY_BORDER_ROUND;

    if (arg_count == 6) {
        tty_require_number("button", args, 5);
        style = (int)tty_as_number(args[5]);
    }

    if (w < 4) {
        return make_null();
    }

    if (active) {
        tty_write_ansi("\x1b[7m");
    }

    RuntimeValue box_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(3),
        make_int(style)
    };

    n_box(ctx, box_args, 5);

    RuntimeValue text_args[6] = {
        make_int(x + 1),
        make_int(y + 1),
        make_int(w - 2),
        args[3],
        make_int(TTY_ALIGN_CENTER),
        make_int(1)
    };

    n_text(ctx, text_args, 6);

    if (active) {
        tty_write_ansi(TTY_ESC_RESET);
    }

    return make_null();
}

static RuntimeValue n_button_flat(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 5) {
        runtime_error("tty.button_flat expects 5 arguments");
    }

    tty_require_number("button_flat", args, 0);
    tty_require_number("button_flat", args, 1);
    tty_require_number("button_flat", args, 2);
    tty_require_string("button_flat", args, 3);
    tty_require_number("button_flat", args, 4);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    int active = (int)tty_as_number(args[4]);

    if (w < 4) {
        return make_null();
    }

    if (active) {
        tty_write_ansi("\x1b[7m");
    } else {
        tty_write_ansi("\x1b[2m");
    }

    tty_goto(x, y);
    fputs("[", stdout);

    RuntimeValue text_args[6] = {
        make_int(x + 1),
        make_int(y),
        make_int(w - 2),
        args[3],
        make_int(TTY_ALIGN_CENTER),
        make_int(1)
    };

    n_text(ctx, text_args, 6);

    tty_goto(x + w - 1, y);
    fputs("]", stdout);
    tty_write_ansi(TTY_ESC_RESET);

    return make_null();
}

static RuntimeValue n_input_box(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 6 && arg_count != 7) {
        runtime_error("tty.input_box expects 6 or 7 arguments");
    }

    tty_require_number("input_box", args, 0);
    tty_require_number("input_box", args, 1);
    tty_require_number("input_box", args, 2);
    tty_require_string("input_box", args, 3);
    tty_require_string("input_box", args, 4);
    tty_require_number("input_box", args, 5);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    int focused = (int)tty_as_number(args[5]);

    int style = focused ? TTY_BORDER_HEAVY : TTY_BORDER_SINGLE;

    if (arg_count == 7) {
        tty_require_number("input_box", args, 6);
        style = (int)tty_as_number(args[6]);
    }

    if (w < 4) {
        return make_null();
    }

    RuntimeValue box_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        make_int(3),
        make_int(style)
    };

    n_box(ctx, box_args, 5);

    RuntimeValue label_args[5] = {
        make_int(x),
        make_int(y),
        make_int(w),
        args[3],
        make_int(TTY_ALIGN_LEFT)
    };

    n_title(ctx, label_args, 5);

    RuntimeValue value_args[6] = {
        make_int(x + 1),
        make_int(y + 1),
        make_int(w - 2),
        args[4],
        make_int(TTY_ALIGN_LEFT),
        make_int(1)
    };

    n_text(ctx, value_args, 6);

    return make_null();
}

static RuntimeValue n_input_line(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    if (arg_count != 7) {
        runtime_error("tty.input_line expects 7 arguments");
    }

    tty_require_number("input_line", args, 0);
    tty_require_number("input_line", args, 1);
    tty_require_number("input_line", args, 2);
    tty_require_string("input_line", args, 3);
    tty_require_string("input_line", args, 4);
    tty_require_number("input_line", args, 5);
    tty_require_number("input_line", args, 6);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long w = tty_as_number(args[2]);
    long long cursor = tty_as_number(args[5]);
    int focused = (int)tty_as_number(args[6]);

    if (w < 8) {
        return make_null();
    }

    size_t label_len = 0;
    const char *label = tty_as_string(args[3], &label_len);
    size_t value_len = 0;
    const char *value = tty_as_string(args[4], &value_len);

    long long label_w = tty_display_width(label, label_len);
    long long field_x = x;
    long long field_w = w;

    tty_goto(x, y);
    tty_repeat(" ", w);

    if (label_len > 0 && label_w + 2 < w) {
        tty_goto(x, y);
        tty_write_clip(label, label_len, label_w);
        fputs(" ", stdout);
        field_x = x + label_w + 1;
        field_w = w - label_w - 1;
    }

    if (field_w < 4) {
        return make_null();
    }

    long long inner_w = field_w - 2;
    long long value_w = tty_display_width(value, value_len);
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > value_w) {
        cursor = value_w;
    }

    long long scroll = 0;
    if (cursor >= inner_w) {
        scroll = cursor - inner_w + 1;
    }

    size_t start = tty_byte_offset_for_columns(value, value_len, scroll);
    long long skipped = tty_display_width(value, start);
    size_t prefix = tty_prefix_bytes_for_columns(value + start, value_len - start, inner_w, NULL);
    long long cursor_col = cursor - skipped;
    if (cursor_col < 0) {
        cursor_col = 0;
    }
    if (cursor_col > inner_w) {
        cursor_col = inner_w;
    }

    if (focused) {
        tty_write_ansi("\x1b[1m");
    } else {
        tty_write_ansi("\x1b[2m");
    }

    tty_goto(field_x, y);
    fputs(" ", stdout);
    tty_repeat(" ", inner_w);
    fputs(" ", stdout);

    tty_goto(field_x + 1, y);
    if (prefix > 0) {
        fwrite(value + start, 1, prefix, stdout);
    }

    if (focused) {
        tty_goto(field_x + 1 + cursor_col, y);
        tty_write_ansi("\x1b[7m");
        if (cursor >= value_w) {
            fputs(" ", stdout);
        } else {
            size_t caret_offset = tty_byte_offset_for_columns(value, value_len, cursor);
            size_t used = 0;
            tty_decode_utf8(value + caret_offset, value_len - caret_offset, &used);
            if (used > 0) {
                fwrite(value + caret_offset, 1, used, stdout);
            } else {
                fputs(" ", stdout);
            }
        }
        tty_write_ansi(TTY_ESC_RESET);
    } else {
        tty_write_ansi(TTY_ESC_RESET);
    }

    return make_null();
}

static RuntimeValue n_badge(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("badge", arg_count, 4);
    tty_require_number("badge", args, 0);
    tty_require_number("badge", args, 1);
    tty_require_string("badge", args, 2);
    tty_require_number("badge", args, 3);

    long long x = tty_as_number(args[0]);
    long long y = tty_as_number(args[1]);
    long long color = tty_as_number(args[3]);

    if (color < 0 || color > 255) {
        runtime_error("tty.badge: color code must be 0..255");
    }

    size_t len = 0;
    const char *text = tty_as_string(args[2], &len);

    tty_goto(x, y);
    printf("\x1b[48;5;%lldm\x1b[38;5;15m", color);
    fputs(" ", stdout);
    fwrite(text, 1, len, stdout);
    fputs(" ", stdout);
    tty_write_ansi(TTY_ESC_RESET);

    return make_null();
}

static RuntimeValue n_color_pair(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("color_pair", arg_count, 2);
    tty_require_number("color_pair", args, 0);
    tty_require_number("color_pair", args, 1);

    long long fg = tty_as_number(args[0]);
    long long bg = tty_as_number(args[1]);

    if (fg < 0 || fg > 255) {
        runtime_error("tty.color_pair: fg color code must be 0..255");
    }

    if (bg < 0 || bg > 255) {
        runtime_error("tty.color_pair: bg color code must be 0..255");
    }

    printf("\x1b[38;5;%lldm\x1b[48;5;%lldm", fg, bg);
    return make_null();
}

static RuntimeValue n_save_cursor(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("save_cursor", arg_count, 0);

    tty_write_ansi("\x1b[s");
    return make_null();
}

static RuntimeValue n_restore_cursor(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    (void)args;

    tty_require_arg_count("restore_cursor", arg_count, 0);

    tty_write_ansi("\x1b[u");
    return make_null();
}

static RuntimeValue n_scroll_up(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("scroll_up", arg_count, 1);
    tty_require_number("scroll_up", args, 0);

    long long n = tty_as_number(args[0]);
    if (n < 1) {
        n = 1;
    }

    printf("\x1b[%lldS", n);
    return make_null();
}

static RuntimeValue n_scroll_down(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;

    tty_require_arg_count("scroll_down", arg_count, 1);
    tty_require_number("scroll_down", args, 0);

    long long n = tty_as_number(args[0]);
    if (n < 1) {
        n = 1;
    }

    printf("\x1b[%lldT", n);
    return make_null();
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_tty(RuntimeValue exports, Environment *module_env) {
    (void)module_env;

    bind(exports, "set_raw", n_set_raw);
    bind(exports, "setraw", n_set_raw);
    bind(exports, "raw", n_raw);
    bind(exports, "cooked", n_cooked);
    bind(exports, "restore", n_restore);

    bind(exports, "readch", n_readch);
    bind(exports, "read_key", n_read_key);
    bind(exports, "read_key_timeout", n_read_key_timeout);
    bind(exports, "edit_text", n_edit_text);

    bind(exports, "write", n_write);
    bind(exports, "write_at", n_write_at);
    bind(exports, "text_at", n_text_at);
    bind(exports, "text", n_text);
    bind(exports, "text_wrap", n_text_wrap);
    bind(exports, "flush", n_flush);

    bind(exports, "clear", n_clear);
    bind(exports, "clear_line", n_clear_line);
    bind(exports, "move", n_move);
    bind(exports, "size", n_size);
    bind(exports, "size_or", n_size_or);
    bind(exports, "is_tty", n_is_tty);

    bind(exports, "hide_cursor", n_hide_cursor);
    bind(exports, "show_cursor", n_show_cursor);
    bind(exports, "save_cursor", n_save_cursor);
    bind(exports, "restore_cursor", n_restore_cursor);

    bind(exports, "alt_screen", n_alt_screen);
    bind(exports, "normal_screen", n_normal_screen);

    bind(exports, "fg", n_fg);
    bind(exports, "bg", n_bg);
    bind(exports, "color_pair", n_color_pair);
    bind(exports, "style", n_style);
    bind(exports, "bold", n_bold);
    bind(exports, "dim", n_dim);
    bind(exports, "underline", n_underline);
    bind(exports, "reverse", n_reverse);
    bind(exports, "reset", n_reset);

    bind(exports, "rect", n_rect);
    bind(exports, "fill_rect", n_rect);
    bind(exports, "erase_rect", n_erase_rect);
    bind(exports, "hline", n_hline);
    bind(exports, "vline", n_vline);

    bind(exports, "box", n_box);
    bind(exports, "box_fill", n_box_fill);
    bind(exports, "title", n_title);
    bind(exports, "window", n_window);
    bind(exports, "shadow", n_shadow);
    bind(exports, "floating_window", n_floating_window);

    bind(exports, "button", n_button);
    bind(exports, "button_flat", n_button_flat);
    bind(exports, "input_box", n_input_box);
    bind(exports, "input_line", n_input_line);
    bind(exports, "badge", n_badge);

    bind(exports, "scroll_up", n_scroll_up);
    bind(exports, "scroll_down", n_scroll_down);
}
