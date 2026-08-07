/* Turns a captured card into a verdict a person can act on.
 *
 * The grade is the product. A hex dump proves nothing to anyone who is not
 * already convinced; "your card handed a stranger the number and the expiry
 * date, which is what an online checkout asks for" does. So the scoring lives
 * in its own Flipper-free file and is checked exhaustively by test/, rather
 * than eyeballed on a screenshot.
 *
 * Flipper-free by design — see test/host_leak_test.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emv_card.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LeakGradeAPlus = 0,
    LeakGradeA,
    LeakGradeB,
    LeakGradeC,
    LeakGradeD,
    LeakGradeF,
} LeakGrade;

/* Ordered by how much damage the field does, because this is also the order
 * the details screen lists them in. */
typedef enum {
    LeakFieldPan = 0,
    LeakFieldExpiry,
    LeakFieldName,
    LeakFieldLog,
    LeakFieldMerchant,
    LeakFieldServiceCode,
    LeakFieldAtc,
    LeakFieldPinTry,
    LeakFieldCountry,
    LeakFieldCurrency,
    LeakFieldApp,
    LeakFieldCount,
} LeakField;

typedef struct {
    bool leaked[LeakFieldCount];
    uint8_t points[LeakFieldCount]; /* what each field contributed */
    uint8_t score; /* 0..100 */
    uint8_t leaked_count;
    LeakGrade grade;
    /* PAN (Luhn-valid) plus expiry: the pair an online checkout asks for.
     * This is the single fact the whole application exists to demonstrate. */
    bool cnp_capable;
} LeakReport;

void leak_grade(const EmvCard* card, LeakReport* out);

const char* leak_grade_name(LeakGrade grade); /* "A+", "D", "F" */
const char* leak_grade_verdict(LeakGrade grade); /* one line, for the result screen */

const char* leak_field_name(LeakField field); /* "Card number" */
const char* leak_field_what(LeakField field); /* what the field is */
const char* leak_field_risk(LeakField field); /* what it buys an attacker */
const char* leak_field_defence(LeakField field); /* what actually stops it */

/* The other half of an honest answer: things a contactless read cannot
 * produce, no matter how good the reader is. */
typedef enum {
    LeakSafeCvv = 0,
    LeakSafePin,
    LeakSafeReplay,
    LeakSafeBalance,
    LeakSafeCount,
} LeakSafeFact;

const char* leak_safe_name(LeakSafeFact fact);
const char* leak_safe_text(LeakSafeFact fact);

#ifdef __cplusplus
}
#endif
