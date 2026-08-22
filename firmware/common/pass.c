#include "pass.h"

#include <stdio.h>

void demo_pass(const char *id)
{
    printf("-- PASS %s\n", id ? id : "?");
    fflush(stdout);
}

void demo_fail(const char *id, const char *reason)
{
    printf("-- FAIL %s %s\n", id ? id : "?", reason ? reason : "");
    fflush(stdout);
}
