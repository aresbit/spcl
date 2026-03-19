#ifndef SP_COMPAT_H
#define SP_COMPAT_H

#include "sp.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} spc_strbuf;

static inline void *spc_malloc(size_t n) {
    if (n > UINT32_MAX) {
        return NULL;
    }
    return sp_alloc((u32)n);
}

static inline void *spc_calloc(size_t n, size_t size) {
    if (n != 0 && size > SIZE_MAX / n) {
        return NULL;
    }
    return spc_malloc(n * size);
}

static inline void *spc_realloc(void *ptr, size_t n) {
    if (n > UINT32_MAX) {
        return NULL;
    }
    return sp_realloc(ptr, (u32)n);
}

static inline void spc_free(void *ptr) {
    if (!ptr) {
        return;
    }
    sp_free(ptr);
}

static inline size_t spc_strlen(const char *s) {
    return (size_t)sp_cstr_len(s);
}

static inline int spc_streq(const char *a, const char *b) {
    return sp_cstr_equal(a, b) ? 1 : 0;
}

static inline int spc_strcaseeq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static inline const char *spc_strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) {
        if (*s == ch) {
            return s;
        }
        ++s;
    }
    return ch == '\0' ? s : NULL;
}

static inline char *spc_substr_dup(const char *s, size_t n) {
    if (n > UINT32_MAX) {
        return NULL;
    }
    return (char *)sp_cstr_copy_sized(s, (u32)n);
}

static inline char *spc_strdup(const char *s) {
    return (char *)sp_cstr_copy(s);
}

static inline int spc_is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline char *spc_trim_dup(const char *s, size_t n) {
    size_t start = 0;
    size_t end = n;
    while (start < end && spc_is_blank(s[start])) {
        ++start;
    }
    while (end > start && spc_is_blank(s[end - 1])) {
        --end;
    }
    return spc_substr_dup(s + start, end - start);
}

static inline char *spc_rtrim_dup(const char *s, size_t n) {
    size_t end = n;
    while (end > 0 && spc_is_blank(s[end - 1])) {
        --end;
    }
    return spc_substr_dup(s, end);
}

static inline bool spc_sb_ensure(spc_strbuf *sb, size_t extra) {
    if (extra > SIZE_MAX - sb->len - 1) {
        return false;
    }

    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return true;
    }

    size_t ncap = sb->cap == 0 ? 64 : sb->cap;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) {
            ncap = need;
            break;
        }
        ncap *= 2;
    }

    if (ncap < need) {
        return false;
    }

    char *nbuf = (char *)spc_realloc(sb->data, ncap);
    if (!nbuf) {
        return false;
    }
    sb->data = nbuf;
    sb->cap = ncap;
    return true;
}

static inline bool spc_sb_init(spc_strbuf *sb, size_t initial_cap) {
    if (initial_cap == 0) {
        initial_cap = 64;
    }
    sb->data = (char *)spc_malloc(initial_cap);
    if (!sb->data) {
        sb->len = 0;
        sb->cap = 0;
        return false;
    }
    sb->data[0] = '\0';
    sb->len = 0;
    sb->cap = initial_cap;
    return true;
}

static inline void spc_sb_free(spc_strbuf *sb) {
    spc_free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static inline bool spc_sb_push_char(spc_strbuf *sb, char c) {
    if (!spc_sb_ensure(sb, 1)) {
        return false;
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return true;
}

static inline bool spc_sb_push_mem(spc_strbuf *sb, const char *s, size_t n) {
    if (!spc_sb_ensure(sb, n)) {
        return false;
    }
    sp_memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return true;
}

static inline bool spc_sb_push_repeat(spc_strbuf *sb, char c, size_t n) {
    if (!spc_sb_ensure(sb, n)) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        sb->data[sb->len++] = c;
    }
    sb->data[sb->len] = '\0';
    return true;
}

static inline char *spc_sb_steal(spc_strbuf *sb) {
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static inline char *spc_read_all(FILE *f, char **error) {
    spc_strbuf sb;
    if (!spc_sb_init(&sb, 4096)) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return NULL;
    }

    char tmp[4096];
    for (;;) {
        size_t n = fread(tmp, 1, sizeof(tmp), f);
        if (n > 0 && !spc_sb_push_mem(&sb, tmp, n)) {
            spc_sb_free(&sb);
            if (error) {
                *error = spc_strdup("Out of memory");
            }
            return NULL;
        }
        if (n < sizeof(tmp)) {
            if (ferror(f)) {
                spc_sb_free(&sb);
                if (error) {
                    *error = spc_strdup("Read error");
                }
                return NULL;
            }
            break;
        }
    }

    return spc_sb_steal(&sb);
}

#endif
