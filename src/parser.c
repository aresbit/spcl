#include "spcl.h"
#include "sp_compat.h"

#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    const char *src;
    size_t len;
    size_t i;
} parser;

static bool kv_push(spcl_kv_list *list, const spcl_key_val *kv) {
    if (list->len == list->cap) {
        size_t ncap = list->cap == 0 ? 8 : list->cap * 2;
        spcl_key_val *nitems = (spcl_key_val *)spc_realloc(list->items, ncap * sizeof(spcl_key_val));
        if (!nitems) {
            return false;
        }
        list->items = nitems;
        list->cap = ncap;
    }
    list->items[list->len++] = *kv;
    return true;
}

void spcl_kv_list_free(spcl_kv_list *list) {
    if (!list || !list->items) {
        return;
    }
    for (size_t i = 0; i < list->len; ++i) {
        spc_free(list->items[i].key);
        spc_free(list->items[i].value);
    }
    spc_free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void spcl_parse_result_free(spcl_parse_result *result) {
    if (!result) {
        return;
    }
    spc_free(result->error);
    result->error = NULL;
    spcl_kv_list_free(&result->kvs);
}

static void skip_blank(parser *p) {
    while (p->i < p->len) {
        char c = p->src[p->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++p->i;
            continue;
        }
        break;
    }
}

static size_t count_spaces(const char *src, size_t len, size_t i) {
    size_t n = 0;
    while (i + n < len && src[i + n] == ' ') {
        ++n;
    }
    return n;
}

static char *parse_value(parser *p, size_t expected_prefix_len, char **error) {
    while (p->i < p->len && p->src[p->i] == ' ') {
        ++p->i;
    }

    spc_strbuf sb;
    if (!spc_sb_init(&sb, 64)) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return NULL;
    }

    for (;;) {
        size_t line_start = p->i;
        while (p->i < p->len && p->src[p->i] != '\n') {
            ++p->i;
        }
        if (!spc_sb_push_mem(&sb, p->src + line_start, p->i - line_start)) {
            spc_sb_free(&sb);
            if (error) {
                *error = spc_strdup("Out of memory");
            }
            return NULL;
        }

        if (p->i >= p->len) {
            break;
        }

        ++p->i;
        size_t spaces = count_spaces(p->src, p->len, p->i);
        p->i += spaces;

        char next = p->i < p->len ? p->src[p->i] : '\0';
        if (spaces == 0 && next == '\n') {
            if (!spc_sb_push_char(&sb, '\n')) {
                spc_sb_free(&sb);
                if (error) {
                    *error = spc_strdup("Out of memory");
                }
                return NULL;
            }
            continue;
        }
        if (spaces <= expected_prefix_len) {
            break;
        }

        if (!spc_sb_push_char(&sb, '\n') || !spc_sb_push_repeat(&sb, ' ', spaces)) {
            spc_sb_free(&sb);
            if (error) {
                *error = spc_strdup("Out of memory");
            }
            return NULL;
        }
    }

    char *out = spc_rtrim_dup(sb.data, sb.len);
    spc_sb_free(&sb);
    if (!out && error) {
        *error = spc_strdup("Out of memory");
    }
    return out;
}

static bool parse_key_val(parser *p, size_t prefix_len, spcl_key_val *out, char **error) {
    size_t key_start = p->i;
    while (p->i < p->len && p->src[p->i] != '=') {
        ++p->i;
    }
    if (p->i >= p->len) {
        if (error) {
            *error = spc_strdup("Parse error: expected '='");
        }
        return false;
    }

    char *key = spc_trim_dup(p->src + key_start, p->i - key_start);
    if (!key) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    skip_blank(p);
    if (p->i >= p->len || p->src[p->i] != '=') {
        spc_free(key);
        if (error) {
            *error = spc_strdup("Parse error: expected '=' after key");
        }
        return false;
    }
    ++p->i;

    char *value = parse_value(p, prefix_len, error);
    if (!value) {
        spc_free(key);
        return false;
    }

    skip_blank(p);

    out->key = key;
    out->value = value;
    out->force_string = false;
    out->force_nested = false;
    return true;
}

static size_t line_start_at(const char *src, size_t i) {
    while (i > 0 && src[i - 1] != '\n') {
        --i;
    }
    return i;
}

static size_t line_end_at(const char *src, size_t len, size_t i) {
    while (i < len && src[i] != '\n') {
        ++i;
    }
    return i;
}

static bool try_parse_spcl_block(parser *p, spcl_kv_list *list, char **error) {
    if (p->i >= p->len) {
        return false;
    }

    size_t ls = line_start_at(p->src, p->i);
    size_t le = line_end_at(p->src, p->len, p->i);
    size_t indent = count_spaces(p->src, p->len, ls);

    char *trimmed = spc_trim_dup(p->src + ls, le - ls);
    if (!trimmed) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    bool is_skills = spc_strcaseeq(trimmed, "skills:");
    bool is_prompt = spc_strcaseeq(trimmed, "prompt:");
    spc_free(trimmed);

    if (!is_skills && !is_prompt) {
        return false;
    }

    p->i = le;
    if (p->i < p->len && p->src[p->i] == '\n') {
        ++p->i;
    }

    spc_strbuf value;
    if (!spc_sb_init(&value, 64)) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    bool first = true;
    while (p->i < p->len) {
        size_t lstart = p->i;
        size_t lend = line_end_at(p->src, p->len, p->i);
        size_t line_indent = count_spaces(p->src, p->len, lstart);

        if (lstart == lend) {
            p->i = lend;
            if (p->i < p->len && p->src[p->i] == '\n') {
                ++p->i;
            }
            if (is_prompt) {
                if (!first && !spc_sb_push_char(&value, '\n')) {
                    spc_sb_free(&value);
                    if (error) {
                        *error = spc_strdup("Out of memory");
                    }
                    return false;
                }
                first = false;
            }
            continue;
        }

        if (line_indent <= indent) {
            break;
        }

        const char *line = p->src + lstart + line_indent;
        size_t line_len = lend - (lstart + line_indent);

        if (is_skills) {
            if (line_len >= 2 && line[0] == '-' && line[1] == ' ') {
                if (first) {
                    if (!spc_sb_push_char(&value, '\n')) {
                        spc_sb_free(&value);
                        if (error) {
                            *error = spc_strdup("Out of memory");
                        }
                        return false;
                    }
                } else if (!spc_sb_push_char(&value, '\n')) {
                    spc_sb_free(&value);
                    if (error) {
                        *error = spc_strdup("Out of memory");
                    }
                    return false;
                }
                if (!spc_sb_push_mem(&value, "  = ", 4) || !spc_sb_push_mem(&value, line + 2, line_len - 2)) {
                    spc_sb_free(&value);
                    if (error) {
                        *error = spc_strdup("Out of memory");
                    }
                    return false;
                }
                first = false;
            }
        } else {
            if (!first && !spc_sb_push_char(&value, '\n')) {
                spc_sb_free(&value);
                if (error) {
                    *error = spc_strdup("Out of memory");
                }
                return false;
            }
            if (!spc_sb_push_mem(&value, line, line_len)) {
                spc_sb_free(&value);
                if (error) {
                    *error = spc_strdup("Out of memory");
                }
                return false;
            }
            first = false;
        }

        p->i = lend;
        if (p->i < p->len && p->src[p->i] == '\n') {
            ++p->i;
        }
    }

    spcl_key_val kv = {0};
    kv.key = spc_strdup(is_skills ? "skills" : "prompt");
    kv.value = spc_sb_steal(&value);
    kv.force_nested = is_skills;
    kv.force_string = is_prompt;

    if (!kv.key || !kv.value || !kv_push(list, &kv)) {
        spc_free(kv.key);
        spc_free(kv.value);
        spc_sb_free(&value);
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    return true;
}

static spcl_parse_result parse_impl(const char *text, bool parse_nested_value) {
    spcl_parse_result result = {
        .ok = false,
        .error = NULL,
        .kvs = {0},
    };

    parser p = {
        .src = text,
        .len = spc_strlen(text),
        .i = 0,
    };

    if (!parse_nested_value) {
        skip_blank(&p);
        while (p.i < p.len) {
            if (try_parse_spcl_block(&p, &result.kvs, &result.error)) {
                skip_blank(&p);
                continue;
            }

            spcl_key_val kv = {0};
            if (!parse_key_val(&p, 0, &kv, &result.error)) {
                spcl_kv_list_free(&result.kvs);
                return result;
            }
            if (!kv_push(&result.kvs, &kv)) {
                spc_free(kv.key);
                spc_free(kv.value);
                spcl_kv_list_free(&result.kvs);
                result.error = spc_strdup("Out of memory");
                return result;
            }
        }
        result.ok = true;
        return result;
    }

    if (p.i >= p.len) {
        result.ok = true;
        return result;
    }

    if (p.src[p.i] == '\n') {
        ++p.i;
        size_t prefix_len = count_spaces(p.src, p.len, p.i);
        while (p.i < p.len && p.src[p.i] == ' ') {
            ++p.i;
        }
        while (p.i < p.len) {
            spcl_key_val kv = {0};
            if (!parse_key_val(&p, prefix_len, &kv, &result.error)) {
                spcl_kv_list_free(&result.kvs);
                return result;
            }
            if (!kv_push(&result.kvs, &kv)) {
                spc_free(kv.key);
                spc_free(kv.value);
                spcl_kv_list_free(&result.kvs);
                result.error = spc_strdup("Out of memory");
                return result;
            }
        }
    } else {
        spcl_key_val kv = {0};
        if (!parse_key_val(&p, 0, &kv, &result.error)) {
            spcl_kv_list_free(&result.kvs);
            return result;
        }
        if (!kv_push(&result.kvs, &kv)) {
            spc_free(kv.key);
            spc_free(kv.value);
            spcl_kv_list_free(&result.kvs);
            result.error = spc_strdup("Out of memory");
            return result;
        }
    }

    result.ok = true;
    return result;
}

spcl_parse_result spcl_parse(const char *text) {
    return parse_impl(text, false);
}

spcl_parse_result spcl_parse_value(const char *text) {
    return parse_impl(text, true);
}
