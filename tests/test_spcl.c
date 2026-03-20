#include "spcl.h"
#include "sp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *value;
} expected_kv;

static void assert_query_exists(const spcl_node *root, const char **keys, size_t nkeys) {
    spcl_node *res = spcl_query(root, keys, nkeys);
    assert(res != NULL);
}

static void assert_parse_ok(spcl_parse_result result, const expected_kv *expected, size_t expected_len) {
    assert(result.ok);
    assert(result.error == NULL);
    assert(result.kvs.len == expected_len);
    for (size_t i = 0; i < expected_len; ++i) {
        assert(strcmp(result.kvs.items[i].key, expected[i].key) == 0);
        assert(strcmp(result.kvs.items[i].value, expected[i].value) == 0);
    }
    spcl_parse_result_free(&result);
}

static void assert_parse_fail(spcl_parse_result result) {
    assert(!result.ok);
    assert(result.error != NULL);
    spcl_parse_result_free(&result);
}

static void test_parser_single_from_ocaml(void) {
    expected_kv expected[] = {{"key", "val"}};
    assert_parse_ok(spcl_parse("key=val"), expected, 1);
    assert_parse_ok(spcl_parse("  key  =  val  "), expected, 1);
    assert_parse_ok(spcl_parse("\nkey = val\n"), expected, 1);

    expected_kv empty_value[] = {{"key", ""}};
    assert_parse_ok(spcl_parse("key ="), empty_value, 1);

    expected_kv empty_key[] = {{"", "val"}};
    assert_parse_ok(spcl_parse("= val"), empty_key, 1);

    expected_kv empty_key_value[] = {{"", ""}};
    assert_parse_ok(spcl_parse("="), empty_key_value, 1);

    expected_kv multi_eq[] = {{"a", "b=c"}};
    assert_parse_ok(spcl_parse("a=b=c"), multi_eq, 1);

    expected_kv section[] = {{"", "= Section 2 =="}};
    assert_parse_ok(spcl_parse("== Section 2 =="), section, 1);

    expected_kv comment[] = {{"/", "this is a comment"}};
    assert_parse_ok(spcl_parse("/= this is a comment"), comment, 1);
}

static void test_parser_multiple_and_nested_from_ocaml(void) {
    expected_kv two[] = {{"key1", "val1"}, {"key2", "val2"}};
    assert_parse_ok(spcl_parse("key1 = val1\nkey2 = val2"), two, 2);

    expected_kv list_like[] = {{"", "3"}, {"", "1"}, {"", "2"}};
    assert_parse_ok(spcl_parse("= 3\n= 1\n= 2\n"), list_like, 3);

    expected_kv array_like[] = {{"1", ""}, {"2", ""}, {"3", ""}};
    assert_parse_ok(spcl_parse("1 =\n2 =\n3 =\n"), array_like, 3);

    expected_kv nested_single[] = {{"key", "\n  val"}};
    assert_parse_ok(spcl_parse("key =\n  val\n"), nested_single, 1);

    expected_kv nested_multi[] = {{"key", "\n  line1\n  line2"}};
    assert_parse_ok(spcl_parse("key =\n  line1\n  line2\n"), nested_multi, 1);

    expected_kv nested_skip[] = {{"key", "\n  line1\n\n  line2"}};
    assert_parse_ok(spcl_parse("key =\n  line1\n\n  line2\n"), nested_skip, 1);

    expected_kv deep_nested[] = {
        {"key", "\n  field1 = value1\n  field2 =\n    subfield = x\n    another = y"},
    };
    assert_parse_ok(
        spcl_parse("key =\n  field1 = value1\n  field2 =\n    subfield = x\n    another = y\n"),
        deep_nested,
        1);
}

static void test_parse_value_from_ocaml(void) {
    assert_parse_ok(spcl_parse_value(""), NULL, 0);
    assert_parse_ok(spcl_parse_value("\n"), NULL, 0);

    assert_parse_fail(spcl_parse_value("   "));
    assert_parse_fail(spcl_parse_value("val"));
    assert_parse_fail(spcl_parse_value("val\n  next"));

    expected_kv empty_key_val[] = {{"", ""}};
    assert_parse_ok(spcl_parse_value("="), empty_key_val, 1);

    expected_kv kv_single[] = {{"key", "val"}};
    assert_parse_ok(spcl_parse_value("key = val\n"), kv_single, 1);

    expected_kv kv_multiple[] = {{"key1", "val1"}};
    assert_parse_ok(spcl_parse_value("key1 = val1\nkey2 = val2\n"), kv_multiple, 1);

    expected_kv kv_multiple_indented[] = {{"key1", "val1\n    key2 = val2"}};
    assert_parse_ok(
        spcl_parse_value("    key1 = val1\n    key2 = val2\n"),
        kv_multiple_indented,
        1);

    expected_kv kv_nested[] = {{"key1", "val1\n  inner = some"}};
    assert_parse_ok(spcl_parse_value("key1 = val1\n  inner = some\nkey2 = val2\n"), kv_nested, 1);
}

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

static void test_colon_blocks_not_supported(void) {
    const char *cfg =
        "SKILLS:\n"
        "  - modern-c-makefile\n"
        "  - modern-c-dev\n";

    char *err = NULL;
    spcl_node *root = spcl_decode(cfg, &err);
    assert(root == NULL);
    assert(err != NULL);
    sp_free(err);
}

static void test_fixpoint_stress_from_ocaml(void) {
    const char *cfg =
        "/= This is a CCL document\n"
        "title = CCL Example\n"
        "\n"
        "database =\n"
        "  enabled = true\n"
        "  ports =\n"
        "    = 8000\n"
        "    = 8001\n"
        "    = 8002\n"
        "  limits =\n"
        "    cpu = 1500mi\n"
        "    memory = 10Gb\n"
        "\n"
        "user =\n"
        "  guestId = 42\n"
        "\n"
        "user =\n"
        "  login = chshersh\n"
        "  createdAt = 2024-12-31\n";

    char *err = NULL;
    spcl_node *root = spcl_decode(cfg, &err);
    assert(root != NULL);
    assert(err == NULL);

    const char *q_comment[] = {"/", "This is a CCL document"};
    const char *q_title[] = {"title", "CCL Example"};
    const char *q_enabled[] = {"database", "enabled", "true"};
    const char *q_port_8000[] = {"database", "ports", "", "8000"};
    const char *q_port_8001[] = {"database", "ports", "", "8001"};
    const char *q_port_8002[] = {"database", "ports", "", "8002"};
    const char *q_cpu[] = {"database", "limits", "cpu", "1500mi"};
    const char *q_memory[] = {"database", "limits", "memory", "10Gb"};
    const char *q_guest[] = {"user", "guestId", "42"};
    const char *q_login[] = {"user", "login", "chshersh"};
    const char *q_created[] = {"user", "createdAt", "2024-12-31"};

    assert_query_exists(root, q_comment, 2);
    assert_query_exists(root, q_title, 2);
    assert_query_exists(root, q_enabled, 3);
    assert_query_exists(root, q_port_8000, 4);
    assert_query_exists(root, q_port_8001, 4);
    assert_query_exists(root, q_port_8002, 4);
    assert_query_exists(root, q_cpu, 4);
    assert_query_exists(root, q_memory, 4);
    assert_query_exists(root, q_guest, 3);
    assert_query_exists(root, q_login, 3);
    assert_query_exists(root, q_created, 3);

    spcl_node_free(root);
}

static void test_node_helpers_and_order(void) {
    const char *cfg =
        "compose =\n"
        "  inputs =\n"
        "    = first\n"
        "    = second\n";

    char *err = NULL;
    spcl_node *root = spcl_decode(cfg, &err);
    assert(root != NULL);
    assert(err == NULL);

    const spcl_node *compose = spcl_node_get_const(root, "compose");
    assert(compose != NULL);
    const spcl_node *inputs = spcl_node_get_const(compose, "inputs");
    assert(inputs != NULL);
    const spcl_node *list = spcl_node_get_const(inputs, "");
    assert(list != NULL);
    assert(list->head != NULL);
    assert(strcmp(list->head->key, "first") == 0);
    assert(list->head->next != NULL);
    assert(strcmp(list->head->next->key, "second") == 0);

    spcl_node *skill = spcl_node_new();
    assert(skill != NULL);
    assert(spcl_node_put_child(root, "skill", skill));
    assert(spcl_node_set_scalar(skill, "description", "base"));
    char *description = spcl_node_scalar_dup(spcl_node_get_const(skill, "description"));
    assert(description != NULL);
    assert(strcmp(description, "base") == 0);
    sp_free(description);
    assert(spcl_node_remove(skill, "description"));
    assert(spcl_node_get_const(skill, "description") == NULL);

    spcl_node_free(root);
}

int main(void) {
    test_parser_single_from_ocaml();
    test_parser_multiple_and_nested_from_ocaml();
    test_parse_value_from_ocaml();
    test_basic();
    test_colon_blocks_not_supported();
    test_fixpoint_stress_from_ocaml();
    test_node_helpers_and_order();
    puts("all tests passed");
    return 0;
}
