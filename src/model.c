#include "spcl.h"
#include "sp_compat.h"

#include <stdlib.h>

typedef struct {
    spc_strbuf sb;
    bool ok;
} pretty_ctx;

spcl_node *spcl_node_new(void) {
    return (spcl_node *)spc_calloc(1, sizeof(spcl_node));
}

static spcl_pair *pair_find(spcl_node *node, const char *key) {
    for (spcl_pair *p = node->head; p != NULL; p = p->next) {
        if (spc_streq(p->key, key)) {
            return p;
        }
    }
    return NULL;
}

static const spcl_pair *pair_find_const(const spcl_node *node, const char *key) {
    for (const spcl_pair *p = node->head; p != NULL; p = p->next) {
        if (spc_streq(p->key, key)) {
            return p;
        }
    }
    return NULL;
}

static bool node_set_child(spcl_node *node, const char *key, spcl_node *child) {
    spcl_pair *p = pair_find(node, key);
    if (p) {
        spcl_node_free(p->value);
        p->value = child;
        return true;
    }

    spcl_pair *np = (spcl_pair *)spc_calloc(1, sizeof(spcl_pair));
    if (!np) {
        return false;
    }
    np->key = spc_strdup(key);
    if (!np->key) {
        spc_free(np);
        return false;
    }
    np->value = child;
    np->next = node->head;
    node->head = np;
    return true;
}

void spcl_node_free(spcl_node *node) {
    if (!node) {
        return;
    }
    spcl_pair *p = node->head;
    while (p) {
        spcl_pair *next = p->next;
        spc_free(p->key);
        spcl_node_free(p->value);
        spc_free(p);
        p = next;
    }
    spc_free(node);
}

spcl_node *spcl_node_clone(const spcl_node *src) {
    if (!src) {
        return NULL;
    }
    spcl_node *dst = spcl_node_new();
    if (!dst) {
        return NULL;
    }
    for (const spcl_pair *p = src->head; p != NULL; p = p->next) {
        spcl_node *child = spcl_node_clone(p->value);
        if (!child || !node_set_child(dst, p->key, child)) {
            spcl_node_free(child);
            spcl_node_free(dst);
            return NULL;
        }
    }
    return dst;
}

void spcl_node_merge_into(spcl_node *dst, const spcl_node *src) {
    if (!dst || !src) {
        return;
    }
    for (const spcl_pair *p = src->head; p != NULL; p = p->next) {
        spcl_pair *existing = pair_find(dst, p->key);
        if (!existing) {
            spcl_node *child = spcl_node_clone(p->value);
            if (!child || !node_set_child(dst, p->key, child)) {
                spcl_node_free(child);
                continue;
            }
            continue;
        }
        spcl_node_merge_into(existing->value, p->value);
    }
}

static spcl_node *singleton_value(const char *value) {
    spcl_node *root = spcl_node_new();
    spcl_node *empty = spcl_node_new();
    if (!root || !empty || !node_set_child(root, value, empty)) {
        spcl_node_free(root);
        spcl_node_free(empty);
        return NULL;
    }
    return root;
}

static spcl_node *build_from_kvs(const spcl_kv_list *kvs);

static spcl_node *child_from_kv(const spcl_key_val *kv) {
    if (kv->force_string) {
        return singleton_value(kv->value);
    }

    if (kv->force_nested) {
        spcl_parse_result nested = spcl_parse_value(kv->value);
        if (nested.ok) {
            spcl_node *node = build_from_kvs(&nested.kvs);
            spcl_parse_result_free(&nested);
            return node;
        }
        spcl_parse_result_free(&nested);
        return singleton_value(kv->value);
    }

    spcl_parse_result nested = spcl_parse_value(kv->value);
    if (!nested.ok) {
        spcl_parse_result_free(&nested);
        return singleton_value(kv->value);
    }

    spcl_node *node = build_from_kvs(&nested.kvs);
    spcl_parse_result_free(&nested);
    return node;
}

static spcl_node *build_from_kvs(const spcl_kv_list *kvs) {
    spcl_node *root = spcl_node_new();
    if (!root) {
        return NULL;
    }

    for (size_t i = 0; i < kvs->len; ++i) {
        const spcl_key_val *kv = &kvs->items[i];
        spcl_node *child = child_from_kv(kv);
        if (!child) {
            spcl_node_free(root);
            return NULL;
        }

        spcl_pair *slot = pair_find(root, kv->key);
        if (!slot) {
            if (!node_set_child(root, kv->key, child)) {
                spcl_node_free(child);
                spcl_node_free(root);
                return NULL;
            }
            continue;
        }

        spcl_node_merge_into(slot->value, child);
        spcl_node_free(child);
    }

    return root;
}

spcl_node *spcl_decode(const char *text, char **error) {
    if (error) {
        *error = NULL;
    }

    spcl_parse_result parsed = spcl_parse(text);
    if (!parsed.ok) {
        if (error) {
            *error = spc_strdup(parsed.error ? parsed.error : "Parse error");
        }
        spcl_parse_result_free(&parsed);
        return NULL;
    }

    spcl_node *model = build_from_kvs(&parsed.kvs);
    if (!model && error) {
        *error = spc_strdup("Out of memory");
    }
    spcl_parse_result_free(&parsed);
    return model;
}

spcl_node *spcl_query(const spcl_node *root, const char **keys, size_t nkeys) {
    const spcl_node *cur = root;
    for (size_t i = 0; i < nkeys; ++i) {
        const spcl_pair *p = pair_find_const(cur, keys[i]);
        if (!p) {
            return NULL;
        }
        cur = p->value;
    }
    return (spcl_node *)cur;
}

static void pretty_rec(pretty_ctx *ctx, const spcl_node *node, size_t indent) {
    if (!ctx->ok) {
        return;
    }
    for (const spcl_pair *p = node->head; p != NULL; p = p->next) {
        ctx->ok = spc_sb_push_repeat(&ctx->sb, ' ', indent) &&
                  spc_sb_push_mem(&ctx->sb, p->key, spc_strlen(p->key)) &&
                  spc_sb_push_mem(&ctx->sb, " =\n", 3);
        if (!ctx->ok) {
            return;
        }
        pretty_rec(ctx, p->value, indent + 2);
        if (!ctx->ok) {
            return;
        }
    }
}

char *spcl_pretty(const spcl_node *node) {
    pretty_ctx ctx = {0};
    if (!spc_sb_init(&ctx.sb, 128)) {
        return NULL;
    }
    ctx.ok = true;
    pretty_rec(&ctx, node, 0);
    if (!ctx.ok) {
        spc_sb_free(&ctx.sb);
        return NULL;
    }
    return spc_sb_steal(&ctx.sb);
}
