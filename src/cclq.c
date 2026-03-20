#include "spcl.h"
#include "sp_compat.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    const char **files;
    size_t nfiles;
    const char ***queries;
    size_t *query_lens;
    size_t nqueries;
} query_cli_args;

typedef struct {
    const char *manifest;
    const char *skills_dir;
    const char *out_dir;
} compose_cli_args;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} str_list;

typedef struct {
    char *rel_path;
    spcl_node *node;
} merged_file;

typedef struct {
    merged_file *items;
    size_t len;
    size_t cap;
} merged_file_list;

static void print_help(const char *argv0) {
    SP_LOG("{} compose <manifest.spcl> --skills <dir> --out <dir>", SP_FMT_CSTR(argv0));
    SP_LOG("{} [file [file ..]] <query>", SP_FMT_CSTR(argv0));
    SP_LOG("{} [file [file ..]] -- [query [query ..]]", SP_FMT_CSTR(argv0));
    puts("Default mode queries values in SPCL/CCL files. Queries are '='-separated keys.");
}

static int split_query(const char *q, const char ***out_parts, size_t *out_len) {
    *out_parts = NULL;
    *out_len = 0;

    if (q[0] == '\0') {
        const char **parts = (const char **)spc_calloc(1, sizeof(char *));
        if (!parts) {
            return -1;
        }
        parts[0] = spc_strdup("");
        if (!parts[0]) {
            spc_free(parts);
            return -1;
        }
        *out_parts = parts;
        *out_len = 1;
        return 0;
    }

    sp_dyn_array(sp_str_t) slices = sp_str_split_c8(sp_str_view(q), '=');
    size_t len = (size_t)sp_dyn_array_size(slices);
    if (len == 0) {
        return -1;
    }

    const char **parts = (const char **)spc_calloc(len, sizeof(char *));
    if (!parts) {
        sp_dyn_array_free(slices);
        return -1;
    }

    for (size_t i = 0; i < len; ++i) {
        parts[i] = spc_substr_dup(slices[i].data, slices[i].len);
        if (!parts[i]) {
            for (size_t j = 0; j < i; ++j) {
                spc_free((void *)parts[j]);
            }
            spc_free(parts);
            sp_dyn_array_free(slices);
            return -1;
        }
    }
    sp_dyn_array_free(slices);

    *out_parts = parts;
    *out_len = len;
    return 0;
}

static void free_query_cli(query_cli_args *args) {
    for (size_t i = 0; i < args->nqueries; ++i) {
        for (size_t j = 0; j < args->query_lens[i]; ++j) {
            spc_free((void *)args->queries[i][j]);
        }
        spc_free((void *)args->queries[i]);
    }
    spc_free(args->files);
    spc_free(args->queries);
    spc_free(args->query_lens);
}

static int parse_query_cli(int argc, char **argv, query_cli_args *out) {
    *out = (query_cli_args){0};

    if (argc == 1) {
        out->files = (const char **)spc_calloc(1, sizeof(char *));
        out->queries = (const char ***)spc_calloc(1, sizeof(char **));
        out->query_lens = (size_t *)spc_calloc(1, sizeof(size_t));
        if (!out->files || !out->queries || !out->query_lens) {
            return -1;
        }
        out->files[0] = "/dev/stdin";
        out->nfiles = 1;
        out->queries[0] = NULL;
        out->query_lens[0] = 0;
        out->nqueries = 1;
        return 0;
    }

    int sep = -1;
    for (int i = 1; i < argc; ++i) {
        if (spc_streq(argv[i], "--")) {
            sep = i;
            break;
        }
    }

    if (sep < 0) {
        if (argc == 2) {
            out->files = (const char **)spc_calloc(1, sizeof(char *));
            out->queries = (const char ***)spc_calloc(1, sizeof(char **));
            out->query_lens = (size_t *)spc_calloc(1, sizeof(size_t));
            if (!out->files || !out->queries || !out->query_lens) {
                return -1;
            }
            if (spc_strchr(argv[1], '=') != NULL) {
                out->files[0] = "/dev/stdin";
                out->nfiles = 1;
                if (split_query(argv[1], &out->queries[0], &out->query_lens[0]) != 0) {
                    return -1;
                }
            } else {
                out->files[0] = argv[1];
                out->nfiles = 1;
                out->queries[0] = NULL;
                out->query_lens[0] = 0;
            }
            out->nqueries = 1;
            return 0;
        }

        size_t file_count = (size_t)(argc - 2);
        out->files = (const char **)spc_calloc(file_count, sizeof(char *));
        out->queries = (const char ***)spc_calloc(1, sizeof(char **));
        out->query_lens = (size_t *)spc_calloc(1, sizeof(size_t));
        if (!out->files || !out->queries || !out->query_lens) {
            return -1;
        }
        for (size_t i = 0; i < file_count; ++i) {
            out->files[i] = argv[i + 1];
        }
        out->nfiles = file_count;
        if (split_query(argv[argc - 1], &out->queries[0], &out->query_lens[0]) != 0) {
            return -1;
        }
        out->nqueries = 1;
        return 0;
    }

    size_t file_count = (size_t)(sep - 1);
    size_t query_count = sep == argc - 1 ? 1 : (size_t)(argc - sep - 1);

    out->files = (const char **)spc_calloc(file_count > 0 ? file_count : 1, sizeof(char *));
    out->queries = (const char ***)spc_calloc(query_count, sizeof(char **));
    out->query_lens = (size_t *)spc_calloc(query_count, sizeof(size_t));
    if (!out->files || !out->queries || !out->query_lens) {
        return -1;
    }

    if (file_count == 0) {
        out->files[0] = "/dev/stdin";
        out->nfiles = 1;
    } else {
        for (size_t i = 0; i < file_count; ++i) {
            out->files[i] = argv[i + 1];
        }
        out->nfiles = file_count;
    }

    if (sep == argc - 1) {
        out->queries[0] = NULL;
        out->query_lens[0] = 0;
        out->nqueries = 1;
        return 0;
    }

    for (size_t i = 0; i < query_count; ++i) {
        if (split_query(argv[sep + 1 + (int)i], &out->queries[i], &out->query_lens[i]) != 0) {
            return -1;
        }
    }
    out->nqueries = query_count;
    return 0;
}

static int run_query_mode(query_cli_args *args) {
    spcl_node *merged = spcl_node_new();
    if (!merged) {
        return 1;
    }

    for (size_t i = 0; i < args->nfiles; ++i) {
        char *err = NULL;
        spcl_node *part = spcl_decode_file(args->files[i], &err);
        if (!part) {
            fprintf(stderr, "Decode error in %s: %s\n", args->files[i], err ? err : "unknown");
            spc_free(err);
            spcl_node_free(merged);
            return 1;
        }
        spcl_node_merge_into(merged, part);
        spcl_node_free(part);
    }

    for (size_t i = 0; i < args->nqueries; ++i) {
        spcl_node *res = spcl_query(merged, args->queries[i], args->query_lens[i]);
        if (!res) {
            fprintf(stderr, "Query not found\n");
            spcl_node_free(merged);
            return 2;
        }
        char *pretty = spcl_pretty(res);
        if (!pretty) {
            fprintf(stderr, "Failed to format result\n");
            spcl_node_free(merged);
            return 1;
        }
        fputs(pretty, stdout);
        if (i + 1 < args->nqueries) {
            fputc('\n', stdout);
        }
        spc_free(pretty);
    }

    spcl_node_free(merged);
    return 0;
}

static int parse_compose_cli(int argc, char **argv, compose_cli_args *out) {
    *out = (compose_cli_args){0};

    if (argc < 3) {
        return -1;
    }

    out->manifest = argv[2];

    for (int i = 3; i < argc; ++i) {
        if (spc_streq(argv[i], "--skills") && i + 1 < argc) {
            out->skills_dir = argv[++i];
            continue;
        }
        if (spc_streq(argv[i], "--out") && i + 1 < argc) {
            out->out_dir = argv[++i];
            continue;
        }
        return -1;
    }

    if (!out->manifest || !out->skills_dir || !out->out_dir) {
        return -1;
    }
    return 0;
}

static void str_list_free(str_list *list) {
    for (size_t i = 0; i < list->len; ++i) {
        spc_free(list->items[i]);
    }
    spc_free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static bool str_list_push(str_list *list, char *item) {
    if (list->len == list->cap) {
        size_t next_cap = list->cap == 0 ? 8 : list->cap * 2;
        char **next_items = (char **)spc_realloc(list->items, next_cap * sizeof(char *));
        if (!next_items) {
            return false;
        }
        list->items = next_items;
        list->cap = next_cap;
    }
    list->items[list->len++] = item;
    return true;
}

static int cmp_cstr_ptrs(const void *lhs, const void *rhs) {
    const char *const *a = (const char *const *)lhs;
    const char *const *b = (const char *const *)rhs;
    return strcmp(*a, *b);
}

static int cmp_merged_files(const void *lhs, const void *rhs) {
    const merged_file *a = (const merged_file *)lhs;
    const merged_file *b = (const merged_file *)rhs;
    return strcmp(a->rel_path, b->rel_path);
}

static void merged_file_list_free(merged_file_list *list) {
    for (size_t i = 0; i < list->len; ++i) {
        spc_free(list->items[i].rel_path);
        spcl_node_free(list->items[i].node);
    }
    spc_free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static merged_file *merged_file_find(merged_file_list *list, const char *rel_path) {
    for (size_t i = 0; i < list->len; ++i) {
        if (spc_streq(list->items[i].rel_path, rel_path)) {
            return &list->items[i];
        }
    }
    return NULL;
}

static bool merged_file_list_merge(merged_file_list *list, const char *rel_path, const spcl_node *node) {
    merged_file *slot = merged_file_find(list, rel_path);
    if (slot) {
        spcl_node_merge_into(slot->node, node);
        return true;
    }

    if (list->len == list->cap) {
        size_t next_cap = list->cap == 0 ? 8 : list->cap * 2;
        merged_file *next_items = (merged_file *)spc_realloc(list->items, next_cap * sizeof(merged_file));
        if (!next_items) {
            return false;
        }
        list->items = next_items;
        list->cap = next_cap;
    }

    spcl_node *clone = spcl_node_clone(node);
    char *path_copy = spc_strdup(rel_path);
    if (!clone || !path_copy) {
        spcl_node_free(clone);
        spc_free(path_copy);
        return false;
    }

    list->items[list->len++] = (merged_file){
        .rel_path = path_copy,
        .node = clone,
    };
    return true;
}

static char *join_path2(const char *lhs, const char *rhs) {
    size_t lhs_len = spc_strlen(lhs);
    size_t rhs_len = spc_strlen(rhs);
    bool need_sep = lhs_len > 0 && lhs[lhs_len - 1] != '/';
    size_t total = lhs_len + (need_sep ? 1 : 0) + rhs_len;
    char *out = (char *)spc_calloc(total + 1, sizeof(char));
    if (!out) {
        return NULL;
    }

    memcpy(out, lhs, lhs_len);
    if (need_sep) {
        out[lhs_len++] = '/';
    }
    memcpy(out + lhs_len, rhs, rhs_len);
    out[total] = '\0';
    return out;
}

static bool ensure_dir_exists(const char *path, char **error) {
    if (!path || path[0] == '\0') {
        return true;
    }

    char *mutable_path = spc_strdup(path);
    if (!mutable_path) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    for (char *p = mutable_path + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(mutable_path, 0755) != 0 && errno != EEXIST) {
            if (error) {
                *error = spc_strdup("Failed to create directory");
            }
            spc_free(mutable_path);
            return false;
        }
        *p = '/';
    }

    if (mkdir(mutable_path, 0755) != 0 && errno != EEXIST) {
        if (error) {
            *error = spc_strdup("Failed to create directory");
        }
        spc_free(mutable_path);
        return false;
    }

    spc_free(mutable_path);
    return true;
}

static bool ensure_parent_dir(const char *path, char **error) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return true;
    }

    size_t parent_len = (size_t)(slash - path);
    char *parent = spc_substr_dup(path, parent_len);
    if (!parent) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    bool ok = ensure_dir_exists(parent, error);
    spc_free(parent);
    return ok;
}

static bool remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return false;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (spc_streq(entry->d_name, ".") || spc_streq(entry->d_name, "..")) {
                continue;
            }
            char *child = join_path2(path, entry->d_name);
            bool ok = child != NULL && remove_tree(child);
            spc_free(child);
            if (!ok) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);
        return rmdir(path) == 0;
    }

    return unlink(path) == 0;
}

static bool has_spcl_suffix(const char *path) {
    size_t len = spc_strlen(path);
    return len >= 5 && strcmp(path + len - 5, ".spcl") == 0;
}

static bool collect_spcl_files_recursive(const char *base_dir, const char *rel_dir, str_list *out, char **error) {
    char *dir_path = rel_dir[0] == '\0' ? spc_strdup(base_dir) : join_path2(base_dir, rel_dir);
    if (!dir_path) {
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (error) {
            *error = spc_strdup("Failed to open directory");
        }
        spc_free(dir_path);
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (spc_streq(entry->d_name, ".") || spc_streq(entry->d_name, "..")) {
            continue;
        }

        char *next_rel = rel_dir[0] == '\0' ? spc_strdup(entry->d_name) : join_path2(rel_dir, entry->d_name);
        char *next_abs = join_path2(base_dir, next_rel ? next_rel : "");
        struct stat st;
        bool ok = next_rel && next_abs && stat(next_abs, &st) == 0;
        if (!ok) {
            if (error) {
                *error = spc_strdup("Failed to stat path");
            }
            spc_free(next_rel);
            spc_free(next_abs);
            closedir(dir);
            spc_free(dir_path);
            return false;
        }

        if (S_ISDIR(st.st_mode)) {
            bool child_ok = collect_spcl_files_recursive(base_dir, next_rel, out, error);
            spc_free(next_rel);
            spc_free(next_abs);
            if (!child_ok) {
                closedir(dir);
                spc_free(dir_path);
                return false;
            }
            continue;
        }

        if (S_ISREG(st.st_mode) && has_spcl_suffix(next_rel) && !spc_streq(next_rel, "SKILL.spcl")) {
            if (!str_list_push(out, next_rel)) {
                if (error) {
                    *error = spc_strdup("Out of memory");
                }
                spc_free(next_abs);
                closedir(dir);
                spc_free(dir_path);
                return false;
            }
        } else {
            spc_free(next_rel);
        }
        spc_free(next_abs);
    }

    closedir(dir);
    spc_free(dir_path);
    return true;
}

static char *node_scalar_at(const spcl_node *node, const char *key) {
    const spcl_node *child = spcl_node_get_const(node, key);
    return spcl_node_scalar_dup(child);
}

static char *node_nested_scalar_at(const spcl_node *node, const char *outer_key, const char *inner_key) {
    const spcl_node *outer = spcl_node_get_const(node, outer_key);
    if (!outer) {
        return NULL;
    }
    return node_scalar_at(outer, inner_key);
}

static char *append_scalar(const char *lhs, const char *rhs, const char *sep) {
    bool lhs_ok = lhs && lhs[0] != '\0';
    bool rhs_ok = rhs && rhs[0] != '\0';
    if (!lhs_ok && !rhs_ok) {
        return NULL;
    }
    if (!lhs_ok) {
        return spc_strdup(rhs);
    }
    if (!rhs_ok) {
        return spc_strdup(lhs);
    }

    size_t lhs_len = spc_strlen(lhs);
    size_t rhs_len = spc_strlen(rhs);
    size_t sep_len = spc_strlen(sep);
    char *out = (char *)spc_calloc(lhs_len + sep_len + rhs_len + 1, sizeof(char));
    if (!out) {
        return NULL;
    }

    memcpy(out, lhs, lhs_len);
    memcpy(out + lhs_len, sep, sep_len);
    memcpy(out + lhs_len + sep_len, rhs, rhs_len);
    return out;
}

static bool append_skill_header(spcl_node *base, spcl_node *overlay) {
    char *base_title = node_scalar_at(base, "title");
    char *overlay_title = node_scalar_at(overlay, "title");
    char *merged_title = append_scalar(base_title, overlay_title, " + ");

    char *base_desc = node_nested_scalar_at(base, "skill", "description");
    char *overlay_desc = node_nested_scalar_at(overlay, "skill", "description");
    char *merged_desc = append_scalar(base_desc, overlay_desc, "\n\n");

    if (merged_title && !spcl_node_set_scalar(base, "title", merged_title)) {
        spc_free(base_title);
        spc_free(overlay_title);
        spc_free(merged_title);
        spc_free(base_desc);
        spc_free(overlay_desc);
        spc_free(merged_desc);
        return false;
    }

    if (merged_desc) {
        spcl_node *skill = spcl_node_get(base, "skill");
        if (!skill) {
            skill = spcl_node_new();
            if (!skill || !spcl_node_put_child(base, "skill", skill)) {
                spcl_node_free(skill);
                spc_free(base_title);
                spc_free(overlay_title);
                spc_free(merged_title);
                spc_free(base_desc);
                spc_free(overlay_desc);
                spc_free(merged_desc);
                return false;
            }
        }
        if (!spcl_node_set_scalar(skill, "description", merged_desc)) {
            spc_free(base_title);
            spc_free(overlay_title);
            spc_free(merged_title);
            spc_free(base_desc);
            spc_free(overlay_desc);
            spc_free(merged_desc);
            return false;
        }
    }

    spcl_node_remove(overlay, "title");
    spcl_node *overlay_skill = spcl_node_get(overlay, "skill");
    if (overlay_skill) {
        spcl_node_remove(overlay_skill, "description");
    }

    spc_free(base_title);
    spc_free(overlay_title);
    spc_free(merged_title);
    spc_free(base_desc);
    spc_free(overlay_desc);
    spc_free(merged_desc);
    return true;
}

static spcl_node *compose_skill_nodes(const char *skills_dir, char **skill_names, size_t skill_count, char **error) {
    spcl_node *current = NULL;

    for (size_t i = 0; i < skill_count; ++i) {
        char *skill_dir = join_path2(skills_dir, skill_names[i]);
        char *skill_path = skill_dir ? join_path2(skill_dir, "SKILL.spcl") : NULL;
        char *decode_error = NULL;
        spcl_node *next = skill_path ? spcl_decode_file(skill_path, &decode_error) : NULL;

        spc_free(skill_dir);
        spc_free(skill_path);

        if (!next) {
            if (error) {
                *error = decode_error ? decode_error : spc_strdup("Failed to decode SKILL.spcl");
            } else {
                spc_free(decode_error);
            }
            spcl_node_free(current);
            return NULL;
        }

        if (!current) {
            current = next;
            continue;
        }

        if (!append_skill_header(current, next)) {
            spcl_node_free(current);
            spcl_node_free(next);
            if (error) {
                *error = spc_strdup("Failed to merge skill headers");
            }
            return NULL;
        }
        spcl_node_merge_into(current, next);
        spcl_node_free(next);
    }

    return current;
}

static bool load_and_merge_extra_files(
    const char *skills_dir,
    char **skill_names,
    size_t skill_count,
    merged_file_list *merged,
    char **error
) {
    for (size_t i = 0; i < skill_count; ++i) {
        char *skill_dir = join_path2(skills_dir, skill_names[i]);
        str_list rel_paths = {0};
        bool ok = skill_dir && collect_spcl_files_recursive(skill_dir, "", &rel_paths, error);
        if (!ok) {
            spc_free(skill_dir);
            str_list_free(&rel_paths);
            return false;
        }

        qsort(rel_paths.items, rel_paths.len, sizeof(char *), cmp_cstr_ptrs);
        for (size_t j = 0; j < rel_paths.len; ++j) {
            char *path = join_path2(skill_dir, rel_paths.items[j]);
            char *decode_error = NULL;
            spcl_node *node = path ? spcl_decode_file(path, &decode_error) : NULL;
            if (!node) {
                if (error) {
                    *error = decode_error ? decode_error : spc_strdup("Failed to decode extra .spcl file");
                } else {
                    spc_free(decode_error);
                }
                spc_free(path);
                spc_free(skill_dir);
                str_list_free(&rel_paths);
                return false;
            }
            if (!merged_file_list_merge(merged, rel_paths.items[j], node)) {
                if (error) {
                    *error = spc_strdup("Out of memory");
                }
                spcl_node_free(node);
                spc_free(path);
                spc_free(skill_dir);
                str_list_free(&rel_paths);
                return false;
            }
            spcl_node_free(node);
            spc_free(path);
        }

        spc_free(skill_dir);
        str_list_free(&rel_paths);
    }

    qsort(merged->items, merged->len, sizeof(merged_file), cmp_merged_files);
    return true;
}

static bool write_skill_output(const char *out_dir, spcl_node *skill_node, merged_file_list *extra_files, char **error) {
    if (!ensure_dir_exists(out_dir, error)) {
        return false;
    }

    char *skill_pretty = spcl_pretty(skill_node);
    char *skill_path = join_path2(out_dir, "SKILL.spcl");
    if (!skill_pretty || !skill_path) {
        spc_free(skill_pretty);
        spc_free(skill_path);
        if (error) {
            *error = spc_strdup("Out of memory");
        }
        return false;
    }
    if (!spcl_write_file(skill_path, skill_pretty, error)) {
        spc_free(skill_pretty);
        spc_free(skill_path);
        return false;
    }
    spc_free(skill_pretty);
    spc_free(skill_path);

    for (size_t i = 0; i < extra_files->len; ++i) {
        char *pretty = spcl_pretty(extra_files->items[i].node);
        char *path = join_path2(out_dir, extra_files->items[i].rel_path);
        if (!pretty || !path) {
            spc_free(pretty);
            spc_free(path);
            if (error) {
                *error = spc_strdup("Out of memory");
            }
            return false;
        }
        if (!ensure_parent_dir(path, error) || !spcl_write_file(path, pretty, error)) {
            spc_free(pretty);
            spc_free(path);
            return false;
        }
        spc_free(pretty);
        spc_free(path);
    }

    return true;
}

static bool collect_manifest_inputs(const spcl_node *manifest, str_list *inputs, char **error) {
    const spcl_node *compose = spcl_node_get_const(manifest, "compose");
    const spcl_node *inputs_node = compose ? spcl_node_get_const(compose, "inputs") : NULL;
    const spcl_node *list_node = inputs_node ? spcl_node_get_const(inputs_node, "") : NULL;
    if (!list_node) {
        if (error) {
            *error = spc_strdup("Manifest is missing compose.inputs");
        }
        return false;
    }

    for (const spcl_pair *pair = list_node->head; pair != NULL; pair = pair->next) {
        char *name = spc_strdup(pair->key);
        if (!name || !str_list_push(inputs, name)) {
            spc_free(name);
            if (error) {
                *error = spc_strdup("Out of memory");
            }
            return false;
        }
    }
    return true;
}

static int run_compose_mode(const compose_cli_args *args) {
    char *error = NULL;
    spcl_node *manifest = spcl_decode_file(args->manifest, &error);
    if (!manifest) {
        fprintf(stderr, "Decode error in %s: %s\n", args->manifest, error ? error : "unknown");
        spc_free(error);
        return 1;
    }

    str_list inputs = {0};
    if (!collect_manifest_inputs(manifest, &inputs, &error)) {
        fprintf(stderr, "%s\n", error ? error : "Failed to collect compose inputs");
        spc_free(error);
        spcl_node_free(manifest);
        str_list_free(&inputs);
        return 1;
    }

    const spcl_node *resolve = spcl_node_get_const(manifest, "resolve");
    char *fixpoint_value = resolve ? node_scalar_at(resolve, "fixpoint") : NULL;
    char *max_iter_value = resolve ? node_scalar_at(resolve, "max_iter") : NULL;
    bool use_fixpoint = !fixpoint_value || spc_strcaseeq(fixpoint_value, "true");
    size_t max_iter = max_iter_value ? (size_t)strtoul(max_iter_value, NULL, 10) : 64;
    if (max_iter == 0) {
        max_iter = 1;
    }

    spcl_node *skill_node = NULL;
    merged_file_list extra_files = {0};
    char *prev_snapshot = NULL;
    bool stable = false;

    for (size_t iter = 0; iter < (use_fixpoint ? max_iter : 1); ++iter) {
        spcl_node_free(skill_node);
        merged_file_list_free(&extra_files);
        skill_node = compose_skill_nodes(args->skills_dir, inputs.items, inputs.len, &error);
        if (!skill_node) {
            fprintf(stderr, "%s\n", error ? error : "Failed to compose SKILL.spcl");
            spc_free(error);
            spcl_node_free(manifest);
            str_list_free(&inputs);
            spc_free(fixpoint_value);
            spc_free(max_iter_value);
            spc_free(prev_snapshot);
            return 1;
        }
        if (!load_and_merge_extra_files(args->skills_dir, inputs.items, inputs.len, &extra_files, &error)) {
            fprintf(stderr, "%s\n", error ? error : "Failed to compose extra .spcl files");
            spc_free(error);
            spcl_node_free(skill_node);
            spcl_node_free(manifest);
            str_list_free(&inputs);
            merged_file_list_free(&extra_files);
            spc_free(fixpoint_value);
            spc_free(max_iter_value);
            spc_free(prev_snapshot);
            return 1;
        }

        char *skill_pretty = spcl_pretty(skill_node);
        spc_strbuf snapshot = {0};
        bool ok = skill_pretty && spc_sb_init(&snapshot, 256) &&
                  spc_sb_push_mem(&snapshot, skill_pretty, spc_strlen(skill_pretty));
        for (size_t i = 0; ok && i < extra_files.len; ++i) {
            char *pretty = spcl_pretty(extra_files.items[i].node);
            ok = pretty &&
                 spc_sb_push_mem(&snapshot, "\n@@", 3) &&
                 spc_sb_push_mem(&snapshot, extra_files.items[i].rel_path, spc_strlen(extra_files.items[i].rel_path)) &&
                 spc_sb_push_char(&snapshot, '\n') &&
                 spc_sb_push_mem(&snapshot, pretty, spc_strlen(pretty));
            spc_free(pretty);
        }
        spc_free(skill_pretty);

        if (!ok) {
            spc_sb_free(&snapshot);
            fprintf(stderr, "Failed to build compose snapshot\n");
            spcl_node_free(skill_node);
            spcl_node_free(manifest);
            str_list_free(&inputs);
            merged_file_list_free(&extra_files);
            spc_free(fixpoint_value);
            spc_free(max_iter_value);
            spc_free(prev_snapshot);
            return 1;
        }

        char *current_snapshot = spc_sb_steal(&snapshot);
        if (!use_fixpoint || (prev_snapshot && spc_streq(prev_snapshot, current_snapshot))) {
            stable = true;
            spc_free(prev_snapshot);
            prev_snapshot = current_snapshot;
            break;
        }

        spc_free(prev_snapshot);
        prev_snapshot = current_snapshot;
    }

    if (use_fixpoint && !stable) {
        fprintf(stderr, "ERR_FIXPOINT_NOT_REACHED\n");
        spcl_node_free(skill_node);
        spcl_node_free(manifest);
        str_list_free(&inputs);
        merged_file_list_free(&extra_files);
        spc_free(fixpoint_value);
        spc_free(max_iter_value);
        spc_free(prev_snapshot);
        return 1;
    }

    if (!remove_tree(args->out_dir) && errno != ENOENT) {
        fprintf(stderr, "Failed to clean output directory: %s\n", args->out_dir);
        spcl_node_free(skill_node);
        spcl_node_free(manifest);
        str_list_free(&inputs);
        merged_file_list_free(&extra_files);
        spc_free(fixpoint_value);
        spc_free(max_iter_value);
        spc_free(prev_snapshot);
        return 1;
    }

    if (!write_skill_output(args->out_dir, skill_node, &extra_files, &error)) {
        fprintf(stderr, "%s\n", error ? error : "Failed to write output");
        spc_free(error);
        spcl_node_free(skill_node);
        spcl_node_free(manifest);
        str_list_free(&inputs);
        merged_file_list_free(&extra_files);
        spc_free(fixpoint_value);
        spc_free(max_iter_value);
        spc_free(prev_snapshot);
        return 1;
    }

    spcl_node_free(skill_node);
    spcl_node_free(manifest);
    str_list_free(&inputs);
    merged_file_list_free(&extra_files);
    spc_free(fixpoint_value);
    spc_free(max_iter_value);
    spc_free(prev_snapshot);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && spc_streq(argv[1], "--help")) {
        print_help(argv[0]);
        return 0;
    }

    if (argc >= 2 && spc_streq(argv[1], "compose")) {
        compose_cli_args args;
        if (parse_compose_cli(argc, argv, &args) != 0) {
            print_help(argv[0]);
            return 1;
        }
        return run_compose_mode(&args);
    }

    query_cli_args args;
    int parsed = parse_query_cli(argc, argv, &args);
    if (parsed != 0) {
        fprintf(stderr, "Failed to parse CLI arguments\n");
        return 1;
    }

    int rc = run_query_mode(&args);
    free_query_cli(&args);
    return rc;
}
