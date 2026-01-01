
#include "test_common.h"

/* per-file runners */
void test_util_run(void);
void test_vec_run(void);
void test_kinds_run(void);
void test_land_run(void);
void test_parser_run(void);
void test_dsl_run(void);

static void banner(const char* name)
{
    fprintf(stdout, "== %s ==\n", name);
}

int main(void)
{
    int start_pass = g_test_ctx.passed;
    int start_fail = g_test_ctx.failed;

    banner("test_util");   test_util_run();
    banner("test_vec");    test_vec_run();
    banner("test_kinds");  test_kinds_run();
    banner("test_land");   test_land_run();
    banner("test_parser"); test_parser_run();
    banner("test_dsl");    test_dsl_run();

    int passed = g_test_ctx.passed - start_pass;
    int failed = g_test_ctx.failed - start_fail;

    fprintf(stdout, "\nTOTAL: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
