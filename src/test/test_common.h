
#ifndef BRZ_TEST_COMMON_H
#define BRZ_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

typedef struct TestCtx {
    int passed;
    int failed;
} TestCtx;

extern TestCtx g_test_ctx;

#define TEST_START(name) do { (void)(name); } while(0)

#define TEST_FAIL(msg) do { \
    g_test_ctx.failed++; \
    fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
} while(0)

#define TEST_PASS() do { g_test_ctx.passed++; } while(0)

#define TEST_ASSERT(expr) do { \
    if(!(expr)) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "assertion failed: %s", #expr); \
        TEST_FAIL(_buf); \
    } else { TEST_PASS(); } \
} while(0)

#define TEST_EQ_INT(a,b) do { \
    long long _a=(long long)(a), _b=(long long)(b); \
    if(_a!=_b){ char _buf[256]; snprintf(_buf,sizeof(_buf),"expected %s==%s (%lld vs %lld)",#a,#b,_a,_b); TEST_FAIL(_buf);} else {TEST_PASS();} \
} while(0)

#define TEST_NE_INT(a,b) do { \
    long long _a=(long long)(a), _b=(long long)(b); \
    if(_a==_b){ char _buf[256]; snprintf(_buf,sizeof(_buf),"expected %s!=%s (both %lld)",#a,#b,_a); TEST_FAIL(_buf);} else {TEST_PASS();} \
} while(0)

#define TEST_EQ_SIZE(a,b) do { \
    size_t _a=(size_t)(a), _b=(size_t)(b); \
    if(_a!=_b){ char _buf[256]; snprintf(_buf,sizeof(_buf),"expected %s==%s (%zu vs %zu)",#a,#b,_a,_b); TEST_FAIL(_buf);} else {TEST_PASS();} \
} while(0)

#define TEST_STREQ(a,b) do { \
    const char* _a=(a); const char* _b=(b); \
    if((_a==NULL && _b==NULL)) { TEST_PASS(); } \
    else if(!_a || !_b || strcmp(_a,_b)!=0){ \
        char _buf[256]; snprintf(_buf,sizeof(_buf),"expected strings equal: '%s' vs '%s'", _a?_a:"(null)", _b?_b:"(null)"); \
        TEST_FAIL(_buf); \
    } else { TEST_PASS(); } \
} while(0)

#define TEST_STRNE(a,b) do { \
    const char* _a=(a); const char* _b=(b); \
    if((_a==NULL && _b==NULL)) { TEST_FAIL("expected strings not equal, both NULL"); } \
    else if(!_a || !_b){ TEST_PASS(); } \
    else if(strcmp(_a,_b)==0){ \
        char _buf[256]; snprintf(_buf,sizeof(_buf),"expected strings not equal: '%s'", _a); \
        TEST_FAIL(_buf); \
    } else { TEST_PASS(); } \
} while(0)

/* write file content to a temp path; returns malloc'd path string */
static inline char* brz_test_write_temp(const char* prefix, const char* content)
{
    const char* tmpdir = getenv("TMPDIR");
    if(!tmpdir) tmpdir = "/tmp";

    unsigned r1 = (unsigned)rand();
    unsigned r2 = (unsigned)(uintptr_t)&tmpdir;

    for(int attempt=0; attempt<50; attempt++)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s/%s%u_%u_%d.tmp",
                 tmpdir,
                 prefix?prefix:"brztest_",
                 r1, r2, attempt);

        FILE* f = fopen(buf, "rb");
        if(f){ fclose(f); continue; } /* exists, try another */

        f = fopen(buf, "wb");
        if(!f) continue;

        if(content) fwrite(content, 1, strlen(content), f);
        fclose(f);

        size_t n = strlen(buf);
        char* out = (char*)malloc(n+1);
        if(!out){ remove(buf); return NULL; }
        memcpy(out, buf, n+1);
        return out;
    }
    return NULL;
}

static inline void brz_test_unlink(const char* path)
{
    if(path) remove(path);
}

#endif
