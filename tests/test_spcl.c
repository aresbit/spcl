#include "spcl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_basic(void) {
    const char *cfg =
        "title = demo\n"
        "db =\n"
        "  host = localhost\n"
        "  port = 5432\n";

    char *err = NULL;
    spcl_node *root = spcl_decode(cfg, &err);
    assert(root != NULL);
    assert(err == NULL);

    const char *q1[] = {"db", "host", "localhost"};
    spcl_node *res = spcl_query(root, q1, 3);
    assert(res != NULL);

    spcl_node_free(root);
}

static void test_spcl_blocks(void) {
    const char *cfg =
        "SKILLS:\n"
        "  - modern-c-makefile\n"
        "  - modern-c-dev\n"
        "\n"
        "PROMPT:\n"
        "  Build fast and keep strict warnings.\n";

    char *err = NULL;
    spcl_node *root = spcl_decode(cfg, &err);
    assert(root != NULL);
    assert(err == NULL);

    const char *q_skill[] = {"skills", "", "modern-c-dev"};
    spcl_node *skill = spcl_query(root, q_skill, 3);
    assert(skill != NULL);

    const char *q_prompt[] = {"prompt", "Build fast and keep strict warnings."};
    spcl_node *prompt = spcl_query(root, q_prompt, 2);
    assert(prompt != NULL);

    spcl_node_free(root);
}

int main(void) {
    test_basic();
    test_spcl_blocks();
    puts("all tests passed");
    return 0;
}
