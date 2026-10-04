#include <stdio.h>
#include <string.h>
#include "mtpz.h"
FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *p, const char *m) {
    if (strstr(p, "/.mtpz-data")) return NULL;
    return __real_fopen(p,m);
}
int main(void) {
    int r=mtpz_load_keys();
#ifdef EXPECT_EMBEDDED
    int ok=r==0 && mtpz_keys_available();
#else
    int ok=r!=0 && !mtpz_keys_available();
#endif
    mtpz_free_keys();
    puts(ok ? "PASS credential availability boundary" : "FAIL credential availability boundary");
    return ok?0:1;
}
