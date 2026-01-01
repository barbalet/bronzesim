
#include "test_common.h"
#include "../brz_util.h"

static void test_streq_cases(void)
{
    TEST_ASSERT(brz_streq("a","a"));
    TEST_ASSERT(brz_streq("",""));
    TEST_ASSERT(!brz_streq("a","b"));
    TEST_ASSERT(!brz_streq("a",""));
    TEST_ASSERT(!brz_streq("","a"));
    TEST_ASSERT(brz_streq(NULL,NULL));
    TEST_ASSERT(!brz_streq(NULL,"a"));
    TEST_ASSERT(!brz_streq("a",NULL));
    TEST_ASSERT(brz_streq("hello","hello"));
    TEST_ASSERT(!brz_streq("hello","hellO"));
    TEST_ASSERT(brz_streq("123","123"));
    TEST_ASSERT(!brz_streq("123","0123"));
    TEST_ASSERT(!brz_streq("abc","abcd"));
    TEST_ASSERT(!brz_streq("abcd","abc"));
}

static void test_strdup_cases(void)
{
    char* p = brz_strdup("hello");
    TEST_ASSERT(p != NULL);
    TEST_STREQ(p, "hello");
    p[0] = 'H';
    TEST_STREQ(p, "Hello");
    free(p);

    TEST_ASSERT(brz_strdup(NULL) == NULL);

    char big[256];
    for(int i=0;i<255;i++) big[i] = (char)('a' + (i%26));
    big[255] = 0;
    char* q = brz_strdup(big);
    TEST_ASSERT(q != NULL);
    TEST_STREQ(q, big);
    free(q);
}

static void test_read_entire_file_cases(void)
{
    size_t n=0;

    /* empty file */
    char* path1 = brz_test_write_temp("brz_empty_", "");
    TEST_ASSERT(path1 != NULL);
    char* s1 = brz_read_entire_file(path1, &n);
    TEST_ASSERT(s1 != NULL);
    TEST_EQ_SIZE(n, 0);
    TEST_STREQ(s1, "");
    free(s1);
    brz_test_unlink(path1);
    free(path1);

    /* small content */
    const char* payload = "line1\nline2\n";
    char* path2 = brz_test_write_temp("brz_small_", payload);
    TEST_ASSERT(path2 != NULL);
    n=123;
    char* s2 = brz_read_entire_file(path2, &n);
    TEST_ASSERT(s2 != NULL);
    TEST_EQ_SIZE(n, strlen(payload));
    TEST_STREQ(s2, payload);
    free(s2);
    brz_test_unlink(path2);
    free(path2);

    /* non-existent */
    n=77;
    char* s3 = brz_read_entire_file("/definitely/not/a/real/path____", &n);
    TEST_ASSERT(s3 == NULL);
}

static void test_rng_determinism(void)
{
    BrzRng a,b;
    brz_rng_seed(&a, 1234u);
    brz_rng_seed(&b, 1234u);

    for(int i=0;i<20;i++)
    {
        uint32_t x = brz_rng_u32(&a);
        uint32_t y = brz_rng_u32(&b);
        TEST_EQ_INT(x, y);
    }

    brz_rng_seed(&a, 1u);
    brz_rng_seed(&b, 2u);
    uint32_t x1 = brz_rng_u32(&a);
    uint32_t y1 = brz_rng_u32(&b);
    TEST_NE_INT(x1, y1);
}

static void test_rng_range(void)
{
    BrzRng r;
    brz_rng_seed(&r, 999u);

    /* inclusive bounds */
    for(int i=0;i<200;i++)
    {
        int v = brz_rng_range(&r, 5, 5);
        TEST_EQ_INT(v, 5);
    }

    brz_rng_seed(&r, 42u);
    for(int i=0;i<200;i++)
    {
        int v = brz_rng_range(&r, -3, 3);
        TEST_ASSERT(v >= -3 && v <= 3);
    }

    /* hi < lo should swap */
    brz_rng_seed(&r, 42u);
    for(int i=0;i<200;i++)
    {
        int v = brz_rng_range(&r, 10, -10);
        TEST_ASSERT(v >= -10 && v <= 10);
    }

    /* quick distribution sanity: ensure we hit multiple values */
    brz_rng_seed(&r, 123u);
    bool seen[7]={0};
    for(int i=0;i<500;i++)
    {
        int v = brz_rng_range(&r, 0, 6);
        if(v>=0 && v<=6) seen[v]=true;
    }
    int count=0;
    for(int i=0;i<7;i++) if(seen[i]) count++;
    TEST_ASSERT(count >= 5);
}

void test_util_run(void)
{
    test_streq_cases();
    test_strdup_cases();
    test_read_entire_file_cases();
    test_rng_determinism();
    test_rng_range();
}
