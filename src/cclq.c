#include "spcl.h"
#include "sp_compat.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char **files;
    size_t nfiles;
    const char ***queries;
    size_t *query_lens;
    size_t nqueries;
} cli_args;

static int split_query(const char *q, const char ***out_parts, size_t *out_len) {
    size_t len = 1;
    for (const char *p = q; *p; ++p) {
        if (*p == '=') {
            ++len;
        }
    }

    const char **parts = (const char **)spc_calloc(len, sizeof(char *));
    if (!parts) {
        *out_parts = NULL;
        *out_len = 0;
        return -1;
    }

    size_t idx = 0;
    const char *start = q;
    for (const char *p = q;; ++p) {
        if (*p == '=' || *p == '\0') {
            size_t n = (size_t)(p - start);
            char *piece = spc_substr_dup(start, n);
            if (!piece) {
                for (size_t i = 0; i < idx; ++i) {
                    spc_free((void *)parts[i]);
                }
                spc_free(parts);
                *out_parts = NULL;
                *out_len = 0;
                return -1;
            }
            parts[idx++] = piece;
            start = p + 1;
        }
        if (*p == '\0') {
            break;
        }
    }

    *out_parts = parts;
    *out_len = idx;
    return 0;
}

static void free_cli(cli_args *args) {
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

static int parse_cli(int argc, char **argv, cli_args *out) {
    *out = (cli_args){0};

    if (argc == 2 && spc_streq(argv[1], "--help")) {
        printf("%s [file [file ..]] <query>\n", argv[0]);
        printf("%s [file [file ..]] -- [query [query ..]]\n", argv[0]);
        printf("Query values in SPCL/CCL files. Queries are '='-separated keys.\n");
        return 1;
    }

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

int main(int argc, char **argv) {
    cli_args args;
    int parsed = parse_cli(argc, argv, &args);
    if (parsed == 1) {
        return 0;
    }
    if (parsed != 0) {
        fprintf(stderr, "Failed to parse CLI arguments\n");
        return 1;
    }

    spcl_node *merged = spcl_node_new();
    if (!merged) {
        free_cli(&args);
        return 1;
    }

    for (size_t i = 0; i < args.nfiles; ++i) {
        char *err = NULL;
        spcl_node *part = spcl_decode_file(args.files[i], &err);
        if (!part) {
            fprintf(stderr, "Decode error in %s: %s\n", args.files[i], err ? err : "unknown");
            spc_free(err);
            spcl_node_free(merged);
            free_cli(&args);
            return 1;
        }
        spcl_node_merge_into(merged, part);
        spcl_node_free(part);
    }

    for (size_t i = 0; i < args.nqueries; ++i) {
        spcl_node *res = spcl_query(merged, args.queries[i], args.query_lens[i]);
        if (!res) {
            fprintf(stderr, "Query not found\n");
            spcl_node_free(merged);
            free_cli(&args);
            return 2;
        }
        char *pretty = spcl_pretty(res);
        if (!pretty) {
            fprintf(stderr, "Failed to format result\n");
            spcl_node_free(merged);
            free_cli(&args);
            return 1;
        }
        fputs(pretty, stdout);
        if (i + 1 < args.nqueries) {
            fputc('\n', stdout);
        }
        spc_free(pretty);
    }

    spcl_node_free(merged);
    free_cli(&args);
    return 0;
}
