// check.h: what the host checks share: Check() prints and counts each result, Known() a fault
// the checks have found in the firmware and that is still there, Finish() prints the total and
// gives main's exit code.
#pragma once
#include <cstdio>

static int failures = 0;
static int known = 0;

static void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/** A check of something the firmware gets wrong today, found and not yet fixed: it prints
 *  KNOWN while it fails, without failing the run, and FIXED once it passes, which does fail it,
 *  so the check becomes a plain Check() with the fix */
__attribute__((unused)) static void Known(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "FIXED" : "KNOWN", what);
    if (ok)
        failures++;
    else
        known++;
}

static int Finish()
{
    if (known)
        printf("%d known\n", known);
    if (failures)
        printf("%d failed\n", failures);
    else
        printf("all passed\n");
    return failures ? 1 : 0;
}
