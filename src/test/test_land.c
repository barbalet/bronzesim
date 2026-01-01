
#include "test_common.h"
#include "../brz_land.h"

static void test_basic_generation_and_range(void)
{
    BrzLand land;
    memset(&land, 0, sizeof(land));
    brz_land_seed(&land, 1, 2);
    brz_land_generate(&land);

    /* sample a bunch of points are in [0,255] */
    for(int i=0;i<200;i++)
    {
        int x = (i*73) % BRZ_LAND_DIM;
        int y = (i*191) % BRZ_LAND_DIM;
        uint8_t h = brz_land_height_at(&land, x, y);
        TEST_ASSERT(h <= 255);
    }

    /* compute min/max to ensure not constant */
    uint8_t minv=255, maxv=0;
    for(int y=0;y<BRZ_LAND_DIM;y+=4)
    for(int x=0;x<BRZ_LAND_DIM;x+=4)
    {
        uint8_t h = brz_land_height_at(&land, x, y);
        if(h<minv) minv=h;
        if(h>maxv) maxv=h;
    }
    TEST_ASSERT(maxv > minv);
    TEST_ASSERT(maxv - minv >= 10);
}

static void test_wraparound(void)
{
    BrzLand land;
    memset(&land, 0, sizeof(land));
    brz_land_seed(&land, 10, 20);
    brz_land_generate(&land);

    int x = 123, y = 456;
    uint8_t a = brz_land_height_at(&land, x, y);
    uint8_t b = brz_land_height_at(&land, x + BRZ_LAND_DIM, y);
    uint8_t c = brz_land_height_at(&land, x, y + BRZ_LAND_DIM);
    uint8_t d = brz_land_height_at(&land, x - BRZ_LAND_DIM, y - BRZ_LAND_DIM);

    TEST_EQ_INT(a, b);
    TEST_EQ_INT(a, c);
    TEST_EQ_INT(a, d);

    /* multiple wraps */
    uint8_t e = brz_land_height_at(&land, x + 3*BRZ_LAND_DIM, y - 2*BRZ_LAND_DIM);
    TEST_EQ_INT(a, e);
}

static void test_determinism_same_seed(void)
{
    BrzLand a,b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    brz_land_seed(&a, 7, 8);
    brz_land_seed(&b, 7, 8);
    brz_land_generate(&a);
    brz_land_generate(&b);

    for(int i=0;i<200;i++)
    {
        int x = (i*97) % BRZ_LAND_DIM;
        int y = (i*41) % BRZ_LAND_DIM;
        TEST_EQ_INT(brz_land_height_at(&a, x, y), brz_land_height_at(&b, x, y));
    }
}

static void test_different_seed_changes_map(void)
{
    BrzLand a,b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    brz_land_seed(&a, 1, 1);
    brz_land_seed(&b, 2, 3);
    brz_land_generate(&a);
    brz_land_generate(&b);

    int diffs=0;
    for(int i=0;i<200;i++)
    {
        int x = (i*29) % BRZ_LAND_DIM;
        int y = (i*31) % BRZ_LAND_DIM;
        if(brz_land_height_at(&a, x, y) != brz_land_height_at(&b, x, y)) diffs++;
    }
    TEST_ASSERT(diffs >= 50);
}

static void test_edge_samples_stable(void)
{
    BrzLand land;
    memset(&land, 0, sizeof(land));
    brz_land_seed(&land, 100, 200);
    brz_land_generate(&land);

    /* corners exist and wrap correctly */
    uint8_t tl = brz_land_height_at(&land, 0, 0);
    uint8_t tr = brz_land_height_at(&land, BRZ_LAND_DIM-1, 0);
    uint8_t bl = brz_land_height_at(&land, 0, BRZ_LAND_DIM-1);
    uint8_t br = brz_land_height_at(&land, BRZ_LAND_DIM-1, BRZ_LAND_DIM-1);


    /* wrap equivalence */
    TEST_EQ_INT(tl, brz_land_height_at(&land, BRZ_LAND_DIM, 0));
    TEST_EQ_INT(tl, brz_land_height_at(&land, 0, BRZ_LAND_DIM));
    TEST_EQ_INT(br, brz_land_height_at(&land, -1, -1));
}

void test_land_run(void)
{
    test_basic_generation_and_range();
    test_wraparound();
    test_determinism_same_seed();
    test_different_seed_changes_map();
    test_edge_samples_stable();
}
