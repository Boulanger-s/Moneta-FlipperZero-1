#include "report.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* A tiny append helper so every write is bounds-checked in one place rather
 * than in thirty. `pos` is advanced only by what actually fit. */
static void appendf(char* out, size_t out_len, size_t* pos, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void appendf(char* out, size_t out_len, size_t* pos, const char* fmt, ...) {
    if(*pos + 1 >= out_len) return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out + *pos, out_len - *pos, fmt, args);
    va_end(args);

    if(n < 0) return;
    if((size_t)n >= out_len - *pos) {
        *pos = out_len - 1; /* truncated; stay terminated */
    } else {
        *pos += (size_t)n;
    }
}

size_t report_build(
    const EmvCard* card,
    const LeakReport* report,
    const char* version,
    const char* stamp,
    char* out,
    size_t out_len) {
    if(out == NULL || out_len == 0) return 0;
    out[0] = '\0';
    if(card == NULL || report == NULL) return 0;

    size_t pos = 0;

    appendf(out, out_len, &pos, "Moneta %s - contactless exposure report\n", version ? version : "");
    if(stamp != NULL && stamp[0] != '\0') {
        appendf(out, out_len, &pos, "Recorded %s\n", stamp);
    }
    if(card->is_demo) {
        appendf(out, out_len, &pos, "SOURCE: built-in demo card, not a real one\n");
    }
    appendf(out, out_len, &pos, "\n");

    /* --- the card, with the number deliberately incomplete --- */
    appendf(out, out_len, &pos, "Card      : %s", emv_scheme_name(card->scheme));
    if(card->has_label) appendf(out, out_len, &pos, " (%s)", card->label);
    appendf(out, out_len, &pos, "\n");

    if(card->has_pan) {
        /* Last four digits only. This is the single line in this file that has
         * to stay wrong on purpose, and it is why report_build exists as its
         * own testable function instead of being inlined into a scene. */
        const char* last4 = card->pan + (card->pan_len > 4 ? card->pan_len - 4 : 0);
        appendf(out, out_len, &pos, "Number    : last four %s only\n", last4);
        appendf(
            out,
            out_len,
            &pos,
            "            %u digits, check digit %s\n",
            (unsigned)card->pan_len,
            card->pan_luhn_ok ? "valid" : "invalid");
    } else {
        appendf(out, out_len, &pos, "Number    : not readable\n");
    }

    char exp[16];
    emv_card_format_expiry(card, exp, sizeof(exp));
    appendf(out, out_len, &pos, "Expiry    : %s\n", exp);
    appendf(out, out_len, &pos, "Cardholder: %s\n", card->has_name ? "LEAKED" : "not sent");

    if(card->has_country) {
        const char* c = emv_country_name(card->country);
        if(c) {
            appendf(out, out_len, &pos, "Issued in : %s\n", c);
        } else {
            appendf(out, out_len, &pos, "Issued in : country %u\n", (unsigned)card->country);
        }
    }

    /* --- the verdict --- */
    appendf(
        out,
        out_len,
        &pos,
        "\nExposure  : %u of 100  -  grade %s\n%s\n",
        (unsigned)report->score,
        leak_grade_name(report->grade),
        leak_grade_verdict(report->grade));

    if(report->cnp_capable) {
        appendf(
            out,
            out_len,
            &pos,
            "\nThe number and the expiry date were both readable.\n"
            "That is the pair an online checkout asks for.\n");
    }

    /* --- the itemisation --- */
    appendf(out, out_len, &pos, "\nWhat left the card:\n");
    bool any = false;
    for(int f = 0; f < LeakFieldCount; f++) {
        if(!report->leaked[f]) continue;
        any = true;
        appendf(
            out,
            out_len,
            &pos,
            "  [%2u] %s\n",
            (unsigned)report->points[f],
            leak_field_name((LeakField)f));
    }
    if(!any) appendf(out, out_len, &pos, "  nothing readable\n");

    if(card->log_num > 0) {
        appendf(
            out,
            out_len,
            &pos,
            "\nTransaction log: %u entries readable off the chip\n",
            (unsigned)card->log_num);
    }

    appendf(out, out_len, &pos, "\nWhat no reader can obtain:\n");
    for(int s = 0; s < LeakSafeCount; s++) {
        appendf(out, out_len, &pos, "  %s\n", leak_safe_name((LeakSafeFact)s));
    }

    appendf(
        out,
        out_len,
        &pos,
        "\nCommands sent : %u\nRecords read  : %u\n",
        (unsigned)card->apdu_count,
        (unsigned)card->records_read);

    appendf(
        out,
        out_len,
        &pos,
        "\nThis report never contains the full card number.\n"
        "Moneta reads only; it cannot make a payment.\n");

    return pos;
}
