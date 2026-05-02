#define _POSIX_C_SOURCE 200809L

#include "json.h"

#include "../GC/gc.h"
#include "../Runtime/context.h"
#include "../Runtime/errors.h"
#include "../Runtime/module.h"
#include "../Runtime/value.h"
#include "../Utils/hash.h"
#include "../env/env.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
} JsonParser;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} JsonBuffer;

typedef struct {
    EnvVar **items;
    size_t count;
    size_t cap;
} EnvEntryList;

static void jb_reserve(JsonBuffer *jb, size_t extra) {
    if (jb->len + extra + 1 <= jb->cap) {
        return;
    }

    size_t new_cap = jb->cap == 0 ? 128 : jb->cap;
    while (new_cap < jb->len + extra + 1) {
        new_cap *= 2;
    }

    char *new_data = (char *)realloc(jb->data, new_cap);
    if (new_data == NULL) {
        runtime_error("json: out of memory while building output");
    }

    jb->data = new_data;
    jb->cap = new_cap;
}

static void jb_append_len(JsonBuffer *jb, const char *text, size_t len) {
    jb_reserve(jb, len);
    memcpy(jb->data + jb->len, text, len);
    jb->len += len;
    jb->data[jb->len] = '\0';
}

static void jb_append(JsonBuffer *jb, const char *text) {
    jb_append_len(jb, text, strlen(text));
}

static void jb_append_char(JsonBuffer *jb, char c) {
    jb_reserve(jb, 1);
    jb->data[jb->len++] = c;
    jb->data[jb->len] = '\0';
}

static void json_skip_ws(JsonParser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) {
        p->pos++;
    }
}

static int json_peek(JsonParser *p) {
    json_skip_ws(p);
    if (p->pos >= p->len) {
        return -1;
    }
    return (unsigned char)p->src[p->pos];
}

static int json_take(JsonParser *p, char expected) {
    json_skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == expected) {
        p->pos++;
        return 1;
    }
    return 0;
}

static void json_expect(JsonParser *p, char expected) {
    if (!json_take(p, expected)) {
        runtime_error("json.parse: expected '%c' near offset %lld",
                      expected,
                      (long long)p->pos);
    }
}

static char *json_parse_string_raw(JsonParser *p, size_t *out_len) {
    json_skip_ws(p);
    if (p->pos >= p->len || p->src[p->pos] != '"') {
        runtime_error("json.parse: expected string near offset %lld",
                      (long long)p->pos);
    }
    p->pos++;

    JsonBuffer jb = {0};
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            if (out_len != NULL) {
                *out_len = jb.len;
            }
            if (jb.data == NULL) {
                jb.data = (char *)malloc(1);
                if (jb.data == NULL) {
                    runtime_error("json.parse: out of memory");
                }
                jb.data[0] = '\0';
            }
            return jb.data;
        }

        if (c == '\\') {
            if (p->pos >= p->len) {
                runtime_error("json.parse: unterminated escape near offset %lld",
                              (long long)p->pos);
            }
            char esc = p->src[p->pos++];
            switch (esc) {
                case '"': jb_append_char(&jb, '"'); break;
                case '\\': jb_append_char(&jb, '\\'); break;
                case '/': jb_append_char(&jb, '/'); break;
                case 'b': jb_append_char(&jb, '\b'); break;
                case 'f': jb_append_char(&jb, '\f'); break;
                case 'n': jb_append_char(&jb, '\n'); break;
                case 'r': jb_append_char(&jb, '\r'); break;
                case 't': jb_append_char(&jb, '\t'); break;
                case 'u': {
                    runtime_error("json.parse: \\u escapes are not supported yet");
                    break;
                }
                default:
                    runtime_error("json.parse: invalid escape '\\%c' near offset %lld",
                                  esc,
                                  (long long)(p->pos - 1));
            }
            continue;
        }

        if ((unsigned char)c < 0x20) {
            runtime_error("json.parse: control character in string near offset %lld",
                          (long long)(p->pos - 1));
        }

        jb_append_char(&jb, c);
    }

    runtime_error("json.parse: unterminated string");
    return NULL;
}

static RuntimeValue json_parse_value(JsonParser *p);

static void json_array_push(RuntimeValue arr, RuntimeValue value) {
    ManagedArray *a = arr.data.managed_array;
    if (a->count >= a->capacity) {
        int new_cap = a->capacity > 0 ? a->capacity * 2 : 4;
        RuntimeValue *new_items =
            (RuntimeValue *)realloc(a->elements, sizeof(RuntimeValue) * (size_t)new_cap);
        if (new_items == NULL) {
            runtime_error("json.parse: out of memory growing array");
        }
        a->elements = new_items;
        a->capacity = new_cap;
    }
    a->elements[a->count++] = value;
}

static RuntimeValue json_parse_array(JsonParser *p) {
    json_expect(p, '[');
    RuntimeValue arr = make_array(4);
    gc_push_root(&arr);

    if (json_take(p, ']')) {
        gc_pop_root();
        return arr;
    }

    while (1) {
        RuntimeValue item = json_parse_value(p);
        json_array_push(arr, item);

        if (json_take(p, ']')) {
            break;
        }
        json_expect(p, ',');
    }

    gc_pop_root();
    return arr;
}

static RuntimeValue json_parse_object(JsonParser *p) {
    json_expect(p, '{');
    Environment *obj_env = env_create_child(NULL);
    gc_push_env(obj_env);
    RuntimeValue obj = make_object(obj_env);
    gc_push_root(&obj);

    if (json_take(p, '}')) {
        gc_pop_root();
        gc_pop_env();
        return obj;
    }

    while (1) {
        size_t key_len = 0;
        char *key = json_parse_string_raw(p, &key_len);
        json_expect(p, ':');
        RuntimeValue value = json_parse_value(p);
        env_set_fast(obj_env, key, get_hash(key), value);
        free(key);

        if (json_take(p, '}')) {
            break;
        }
        json_expect(p, ',');
    }

    gc_pop_root();
    gc_pop_env();
    return obj;
}

static RuntimeValue json_parse_number(JsonParser *p) {
    json_skip_ws(p);
    const char *start = p->src + p->pos;
    char *end = NULL;
    errno = 0;
    double parsed = strtod(start, &end);
    if (start == end) {
        runtime_error("json.parse: invalid number near offset %lld",
                      (long long)p->pos);
    }
    if (errno == ERANGE) {
        runtime_error("json.parse: number out of range near offset %lld",
                      (long long)p->pos);
    }

    size_t consumed = (size_t)(end - start);
    p->pos += consumed;
    return make_number_from_double(parsed);
}

static int json_match_keyword(JsonParser *p, const char *kw) {
    json_skip_ws(p);
    size_t kw_len = strlen(kw);
    if (p->pos + kw_len > p->len) {
        return 0;
    }
    if (strncmp(p->src + p->pos, kw, kw_len) != 0) {
        return 0;
    }
    p->pos += kw_len;
    return 1;
}

static RuntimeValue json_parse_value(JsonParser *p) {
    int c = json_peek(p);
    if (c < 0) {
        runtime_error("json.parse: unexpected end of input");
    }

    if (c == '"') {
        size_t len = 0;
        char *text = json_parse_string_raw(p, &len);
        return make_string_owned(text, len);
    }
    if (c == '{') {
        return json_parse_object(p);
    }
    if (c == '[') {
        return json_parse_array(p);
    }
    if (c == 't') {
        if (!json_match_keyword(p, "true")) {
            runtime_error("json.parse: invalid token near offset %lld",
                          (long long)p->pos);
        }
        return make_bool(true);
    }
    if (c == 'f') {
        if (!json_match_keyword(p, "false")) {
            runtime_error("json.parse: invalid token near offset %lld",
                          (long long)p->pos);
        }
        return make_bool(false);
    }
    if (c == 'n') {
        if (!json_match_keyword(p, "null")) {
            runtime_error("json.parse: invalid token near offset %lld",
                          (long long)p->pos);
        }
        return make_null();
    }
    if (c == '-' || isdigit(c)) {
        return json_parse_number(p);
    }

    runtime_error("json.parse: unexpected character '%c' near offset %lld",
                  c,
                  (long long)p->pos);
    return make_null();
}

static void json_escape_string(JsonBuffer *jb, const char *text, size_t len) {
    jb_append_char(jb, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
            case '"': jb_append(jb, "\\\""); break;
            case '\\': jb_append(jb, "\\\\"); break;
            case '\b': jb_append(jb, "\\b"); break;
            case '\f': jb_append(jb, "\\f"); break;
            case '\n': jb_append(jb, "\\n"); break;
            case '\r': jb_append(jb, "\\r"); break;
            case '\t': jb_append(jb, "\\t"); break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    jb_append(jb, buf);
                } else {
                    jb_append_char(jb, (char)c);
                }
                break;
        }
    }
    jb_append_char(jb, '"');
}

static void env_entry_list_push(EnvEntryList *list, EnvVar *entry) {
    if (list->count >= list->cap) {
        size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
        EnvVar **new_items = (EnvVar **)realloc(list->items, sizeof(EnvVar *) * new_cap);
        if (new_items == NULL) {
            runtime_error("json.stringify: out of memory collecting object fields");
        }
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->count++] = entry;
}

static int env_entry_cmp(const void *lhs, const void *rhs) {
    const EnvVar *a = *(const EnvVar *const *)lhs;
    const EnvVar *b = *(const EnvVar *const *)rhs;
    return strcmp(a->name, b->name);
}

static void json_stringify_value(JsonBuffer *jb, RuntimeValue value);

static void json_stringify_object(JsonBuffer *jb, Environment *env) {
    EnvEntryList list = {0};

    if (env != NULL && env->buckets != NULL) {
        for (size_t i = 0; i < env->bucket_count; i++) {
            for (EnvVar *entry = env->buckets[i]; entry != NULL; entry = entry->next) {
                env_entry_list_push(&list, entry);
            }
        }
    }

    qsort(list.items, list.count, sizeof(EnvVar *), env_entry_cmp);

    jb_append_char(jb, '{');
    for (size_t i = 0; i < list.count; i++) {
        if (i > 0) {
            jb_append_char(jb, ',');
        }
        json_escape_string(jb, list.items[i]->name, strlen(list.items[i]->name));
        jb_append_char(jb, ':');
        json_stringify_value(jb, list.items[i]->value);
    }
    jb_append_char(jb, '}');

    free(list.items);
}

static void json_stringify_array(JsonBuffer *jb, ManagedArray *arr) {
    jb_append_char(jb, '[');
    if (arr != NULL) {
        for (int i = 0; i < arr->count; i++) {
            if (i > 0) {
                jb_append_char(jb, ',');
            }
            json_stringify_value(jb, arr->elements[i]);
        }
    }
    jb_append_char(jb, ']');
}

static void json_stringify_value(JsonBuffer *jb, RuntimeValue value) {
    if (value.type == VAL_RETURN) {
        value.type = value.original_type;
    }

    switch (value.type) {
        case VAL_INT: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld", (long long)value.data.int_value);
            jb_append(jb, buf);
            return;
        }
        case VAL_FLOAT: {
            if (!isfinite(value.data.float_value)) {
                runtime_error("json.stringify: non-finite numbers are not supported");
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", value.data.float_value);
            jb_append(jb, buf);
            return;
        }
        case VAL_BOOL:
            jb_append(jb, value.data.bool_value ? "true" : "false");
            return;
        case VAL_STRING:
            if (value.data.managed_string == NULL) {
                jb_append(jb, "\"\"");
                return;
            }
            json_escape_string(jb,
                               value.data.managed_string->data,
                               value.data.managed_string->len);
            return;
        case VAL_ARRAY:
            json_stringify_array(jb, value.data.managed_array);
            return;
        case VAL_OBJECT:
        case VAL_MODULE:
            json_stringify_object(jb, value.data.obj_env);
            return;
        case VAL_NULL:
            jb_append(jb, "null");
            return;
        default:
            runtime_error("json.stringify: unsupported value type");
    }
}

static RuntimeValue n_parse(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("json.parse expects 1 argument");
    }
    if (args[0].type != VAL_STRING) {
        runtime_error("json.parse: text must be string");
    }

    JsonParser p;
    p.src = args[0].data.managed_string != NULL ? args[0].data.managed_string->data : "";
    p.pos = 0;
    p.len = args[0].data.managed_string != NULL ? args[0].data.managed_string->len : 0;

    RuntimeValue value = json_parse_value(&p);
    json_skip_ws(&p);
    if (p.pos != p.len) {
        runtime_error("json.parse: trailing data near offset %lld",
                      (long long)p.pos);
    }
    return value;
}

static RuntimeValue n_stringify(MKSContext *ctx, const RuntimeValue *args, int arg_count) {
    (void)ctx;
    if (arg_count != 1) {
        runtime_error("json.stringify expects 1 argument");
    }

    JsonBuffer jb = {0};
    json_stringify_value(&jb, args[0]);
    if (jb.data == NULL) {
        return make_string("");
    }
    return make_string_owned(jb.data, jb.len);
}

static void bind(RuntimeValue exports, const char *name, NativeFn fn) {
    module_bind_native(exports, name, fn);
}

void std_init_json(RuntimeValue exports, Environment *module_env) {
    (void)module_env;
    bind(exports, "parse", n_parse);
    bind(exports, "stringify", n_stringify);
}
