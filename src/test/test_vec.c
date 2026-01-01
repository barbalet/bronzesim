
#include "test_common.h"
#include "../brz_vec.h"

typedef struct { int a; int b; } Pair;
BRZ_VEC_DECL(Pair, PairVec)

static void test_init_destroy(void)
{
    BrzVec v; memset(&v, 0, sizeof(v));
    brz_vec_init(&v, sizeof(int));
    TEST_EQ_SIZE(brz_vec_len(&v), 0);
    TEST_ASSERT(v.elem_size == sizeof(int));
    brz_vec_destroy(&v);
    TEST_ASSERT(v.data == NULL);
    TEST_EQ_SIZE(v.len, 0);
    TEST_EQ_SIZE(v.cap, 0);
}

static void test_push_at_cat(void)
{
    BrzVec v; brz_vec_init(&v, sizeof(int));

    for(int i=0;i<50;i++)
    {
        TEST_ASSERT(brz_vec_push(&v, &i));
        TEST_EQ_SIZE(brz_vec_len(&v), (size_t)(i+1));
        int* p = (int*)brz_vec_at(&v, (size_t)i);
        TEST_ASSERT(p != NULL);
        TEST_EQ_INT(*p, i);

        const int* cp = (const int*)brz_vec_cat(&v, (size_t)i);
        TEST_ASSERT(cp != NULL);
        TEST_EQ_INT(*cp, i);
    }

    /* out of range */
    TEST_ASSERT(brz_vec_at(&v, 9999) == NULL);
    TEST_ASSERT(brz_vec_cat(&v, 9999) == NULL);

    brz_vec_destroy(&v);
}

static void test_reserve_clear(void)
{
    BrzVec v; brz_vec_init(&v, sizeof(int));

    TEST_ASSERT(brz_vec_reserve(&v, 0));
    TEST_ASSERT(brz_vec_reserve(&v, 10));
    TEST_ASSERT(v.cap >= 10);

    for(int i=0;i<10;i++) TEST_ASSERT(brz_vec_push(&v, &i));
    TEST_EQ_SIZE(v.len, 10);

    brz_vec_clear(&v);
    TEST_EQ_SIZE(v.len, 0);
    TEST_ASSERT(v.data != NULL); /* capacity retained */

    /* can push again after clear */
    int x=7;
    TEST_ASSERT(brz_vec_push(&v, &x));
    TEST_EQ_SIZE(v.len, 1);
    TEST_EQ_INT(*(int*)brz_vec_at(&v,0), 7);

    brz_vec_destroy(&v);
}

static void test_pop(void)
{
    BrzVec v; brz_vec_init(&v, sizeof(int));

    int out=-1;
    TEST_ASSERT(!brz_vec_pop(&v, &out));
    TEST_EQ_INT(out, -1);

    for(int i=0;i<5;i++) brz_vec_push(&v, &i);

    for(int i=4;i>=0;i--)
    {
        out=-1;
        TEST_ASSERT(brz_vec_pop(&v, &out));
        TEST_EQ_INT(out, i);
        TEST_EQ_SIZE(v.len, (size_t)i);
    }

    TEST_ASSERT(!brz_vec_pop(&v, NULL)); /* empty pop should fail */
    brz_vec_destroy(&v);
}

static void test_elem_size_guards(void)
{
    BrzVec v; memset(&v,0,sizeof(v));
    brz_vec_init(&v, 0);
    int x=1;
    TEST_ASSERT(!brz_vec_push(&v, &x));
    TEST_ASSERT(!brz_vec_reserve(&v, 10));
    brz_vec_destroy(&v);
}

static void test_typed_wrapper(void)
{
    PairVec pv; PairVec_init(&pv);
    TEST_EQ_SIZE(PairVec_len(&pv), 0);

    for(int i=0;i<20;i++)
    {
        Pair p = { i, i*i };
        TEST_ASSERT(PairVec_push(&pv, &p));
        TEST_EQ_SIZE(PairVec_len(&pv), (size_t)(i+1));
        Pair* at = PairVec_at(&pv, (size_t)i);
        TEST_ASSERT(at != NULL);
        TEST_EQ_INT(at->a, i);
        TEST_EQ_INT(at->b, i*i);

        const Pair* cat = PairVec_cat(&pv, (size_t)i);
        TEST_ASSERT(cat != NULL);
        TEST_EQ_INT(cat->a, i);
    }

    PairVec_clear(&pv);
    TEST_EQ_SIZE(PairVec_len(&pv), 0);

    PairVec_destroy(&pv);
}

void test_vec_run(void)
{
    test_init_destroy();
    test_push_at_cat();
    test_reserve_clear();
    test_pop();
    test_elem_size_guards();
    test_typed_wrapper();
}
