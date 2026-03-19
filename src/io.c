#include "spcl.h"
#include "sp_compat.h"

#include <stdio.h>
#include <stdlib.h>

spcl_node *spcl_decode_file(const char *path, char **error) {
    if (error) {
        *error = NULL;
    }

    FILE *f = NULL;
    if (spc_streq(path, "-") || spc_streq(path, "/dev/stdin")) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (!f) {
            if (error) {
                *error = spc_strdup("Failed to open file");
            }
            return NULL;
        }
    }

    char *read_error = NULL;
    char *text = spc_read_all(f, &read_error);
    if (f != stdin) {
        fclose(f);
    }
    if (!text) {
        if (error) {
            *error = read_error;
        } else {
            spc_free(read_error);
        }
        return NULL;
    }

    spcl_node *node = spcl_decode(text, error);
    spc_free(text);
    return node;
}
