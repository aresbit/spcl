#ifndef SPCL_H
#define SPCL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spcl_node spcl_node;
typedef struct spcl_pair spcl_pair;

typedef struct {
    char *key;
    char *value;
} spcl_key_val;

typedef struct {
    spcl_key_val *items;
    size_t len;
    size_t cap;
} spcl_kv_list;

typedef struct {
    bool ok;
    char *error;
    spcl_kv_list kvs;
} spcl_parse_result;

struct spcl_pair {
    char *key;
    spcl_node *value;
    spcl_pair *next;
};

struct spcl_node {
    spcl_pair *head;
};

void spcl_kv_list_free(spcl_kv_list *list);
spcl_parse_result spcl_parse(const char *text);
spcl_parse_result spcl_parse_value(const char *text);
void spcl_parse_result_free(spcl_parse_result *result);

spcl_node *spcl_node_new(void);
spcl_node *spcl_node_clone(const spcl_node *src);
void spcl_node_free(spcl_node *node);
void spcl_node_merge_into(spcl_node *dst, const spcl_node *src);
spcl_node *spcl_node_get(spcl_node *node, const char *key);
const spcl_node *spcl_node_get_const(const spcl_node *node, const char *key);
bool spcl_node_put_child(spcl_node *node, const char *key, spcl_node *child);
bool spcl_node_set_scalar(spcl_node *node, const char *key, const char *value);
char *spcl_node_scalar_dup(const spcl_node *node);
bool spcl_node_remove(spcl_node *node, const char *key);

spcl_node *spcl_decode(const char *text, char **error);
spcl_node *spcl_decode_file(const char *path, char **error);
bool spcl_write_file(const char *path, const char *text, char **error);

spcl_node *spcl_query(const spcl_node *root, const char **keys, size_t nkeys);
char *spcl_pretty(const spcl_node *node);

#ifdef __cplusplus
}
#endif

#endif
