
#include "test_common.h"
#include "../brz_kinds.h"

static void test_init_empty(void)
{
    KindTable kt;
    kind_table_init(&kt);
    TEST_EQ_SIZE(kind_table_count(&kt), 0);
    TEST_STREQ(kind_table_name(&kt, 0), "");
    TEST_EQ_INT(kind_table_find(&kt, "nope"), -1);
    kind_table_destroy(&kt);
}

static void test_add_find_name(void)
{
    KindTable kt;
    kind_table_init(&kt);

    TEST_EQ_INT(kind_table_add(&kt, "fish"), 0);
    TEST_EQ_INT(kind_table_add(&kt, "grain"), 1);
    TEST_EQ_INT(kind_table_add(&kt, "wood"), 2);
    TEST_EQ_SIZE(kind_table_count(&kt), 3);

    TEST_EQ_INT(kind_table_find(&kt, "fish"), 0);
    TEST_EQ_INT(kind_table_find(&kt, "grain"), 1);
    TEST_EQ_INT(kind_table_find(&kt, "wood"), 2);
    TEST_EQ_INT(kind_table_find(&kt, "clay"), -1);

    TEST_STREQ(kind_table_name(&kt, 0), "fish");
    TEST_STREQ(kind_table_name(&kt, 1), "grain");
    TEST_STREQ(kind_table_name(&kt, 2), "wood");
    TEST_STREQ(kind_table_name(&kt, 3), "");   /* OOR */
    TEST_STREQ(kind_table_name(&kt, -1), "");  /* OOR */

    kind_table_destroy(&kt);
}

static void test_duplicate_returns_existing(void)
{
    KindTable kt;
    kind_table_init(&kt);

    int a = kind_table_add(&kt, "tin");
    int b = kind_table_add(&kt, "tin");
    TEST_EQ_INT(a, b);
    TEST_EQ_SIZE(kind_table_count(&kt), 1);

    int c = kind_table_add(&kt, "Tin");
    TEST_NE_INT(a, c);
    TEST_EQ_SIZE(kind_table_count(&kt), 2);

    kind_table_destroy(&kt);
}

static void test_many_adds_and_order(void)
{
    KindTable kt;
    kind_table_init(&kt);

    const char* names[] = {"a","b","c","d","e","f","g","h","i","j","k","l","m"};
    int n = (int)(sizeof(names)/sizeof(names[0]));
    for(int i=0;i<n;i++)
    {
        TEST_EQ_INT(kind_table_add(&kt, names[i]), i);
        TEST_EQ_INT(kind_table_find(&kt, names[i]), i);
        TEST_STREQ(kind_table_name(&kt, i), names[i]);
    }
    TEST_EQ_SIZE(kind_table_count(&kt), (size_t)n);

    /* reverse lookups */
    for(int i=n-1;i>=0;i--)
    {
        TEST_EQ_INT(kind_table_find(&kt, names[i]), i);
    }

    kind_table_destroy(&kt);
}

static void test_null_inputs(void)
{
    KindTable kt;
    kind_table_init(&kt);

    TEST_EQ_INT(kind_table_add(NULL, "x"), -1);
    TEST_EQ_INT(kind_table_add(&kt, NULL), -1);
    TEST_EQ_INT(kind_table_find(NULL, "x"), -1);
    TEST_EQ_INT(kind_table_find(&kt, NULL), -1);
    TEST_STREQ(kind_table_name(NULL, 0), "");

    kind_table_destroy(&kt);
}

void test_kinds_run(void)
{
    test_init_empty();
    test_add_find_name();
    test_duplicate_returns_existing();
    test_many_adds_and_order();
    test_null_inputs();
}
