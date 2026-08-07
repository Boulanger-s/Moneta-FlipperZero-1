/* Builds the text of an exposure report.
 *
 * Kept free of Flipper headers for one reason above all others: the promise
 * this file makes — that a saved report never contains the full card number —
 * is worth exactly as much as the test that proves it, and the test needs to
 * run on a host. See test/host_emv_test.c, which builds a report from a card
 * with a known PAN and then searches the output for that PAN.
 *
 * The caller owns the buffer and the clock. Nothing here opens a file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "emv_card.h"
#include "leak_grade.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A comfortable size for the buffer a caller should hand to report_build. */
#define MONETA_REPORT_MAX 2048

/* Render the report into `out`. `stamp` is a human-readable date and time (or
 * NULL). `version` is the application version string. Returns the number of
 * characters written, excluding the terminator; the output is always
 * terminated and never exceeds out_len - 1. */
size_t report_build(
    const EmvCard* card,
    const LeakReport* report,
    const char* version,
    const char* stamp,
    char* out,
    size_t out_len);

#ifdef __cplusplus
}
#endif
