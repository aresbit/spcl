#ifndef SPCLIB_H
#define SPCLIB_H

#include "sp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} spc_strbuf;

void *spc_malloc(size_t n);
void *spc_calloc(size_t n, size_t size);
void *spc_realloc(void *ptr, size_t n);
void spc_free(void *ptr);

size_t spc_strlen(const char *s);
int spc_streq(const char *a, const char *b);
int spc_strcaseeq(const char *a, const char *b);
const char *spc_strchr(const char *s, int c);

char *spc_strdup(const char *s);
char *spc_substr_dup(const char *s, size_t n);
char *spc_trim_dup(const char *s, size_t n);
char *spc_rtrim_dup(const char *s, size_t n);

bool spc_sb_init(spc_strbuf *sb, size_t initial_cap);
void spc_sb_free(spc_strbuf *sb);
bool spc_sb_push_char(spc_strbuf *sb, char c);
bool spc_sb_push_mem(spc_strbuf *sb, const char *s, size_t n);
bool spc_sb_push_repeat(spc_strbuf *sb, char c, size_t n);
char *spc_sb_steal(spc_strbuf *sb);

char *spc_read_all(FILE *f, char **error);

#endif
