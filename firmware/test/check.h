// check.h: what the host checks share: Check() prints and counts each result, Finish()
// prints the total and gives main's exit code.
#pragma once
#include <cstdio>

static int failures = 0;

static void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static int Finish()
{
    if (failures)
        printf("%d failed\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
