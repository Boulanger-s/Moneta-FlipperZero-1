/* Everything Moneta could pull off a contactless payment card, and nothing it
 * could not.
 *
 * Flipper-free on purpose: the radio work lives in emv_reader.c, and this file
 * only ever sees byte buffers. That keeps the interesting half — the half that
 * decides what your card actually gave away — testable on a laptop.
 *
 * There is no CVV field here and there never will be. The three digits on the
 * back of the card are not stored on the chip and are not transmitted over the
 * air, so no reader on earth, including this one, can read them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "emv_tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MONETA_PAN_DIGITS_MAX 19
#define MONETA_NAME_MAX 26
#define MONETA_LABEL_MAX 16
#define MONETA_AID_MAX 16
#define MONETA_APPS_MAX 4
#define MONETA_LOG_MAX 10

typedef enum {
    EmvSchemeUnknown = 0,
    EmvSchemeVisa,
    EmvSchemeMastercard,
    EmvSchemeMaestro,
    EmvSchemeAmex,
    EmvSchemeDiscover,
    EmvSchemeJcb,
    EmvSchemeUnionPay,
    EmvSchemeRuPay,
    EmvSchemeDiners,
    EmvSchemeInterac,
    EmvSchemeMir,
} EmvScheme;

/* One line of the on-card transaction log, where the card keeps one. The
 * layout is not fixed by the standard — the card declares it in tag 9F4F —
 * so every field is optional and flagged. */
#define MONETA_MERCHANT_MAX 20

typedef struct {
    uint32_t amount; /* minor units, e.g. pence/cents; saturates at UINT32_MAX */
    uint16_t currency; /* ISO 4217 numeric */
    uint16_t country; /* ISO 3166 numeric */
    uint8_t year; /* 2-digit, as the card stores it */
    uint8_t month;
    uint8_t day;
    uint16_t atc;
    char merchant[MONETA_MERCHANT_MAX + 1]; /* 9F4E, where the card was used */
    bool has_amount;
    bool has_currency;
    bool has_country;
    bool has_date;
    bool has_atc;
    bool has_merchant;
} EmvLogEntry;

/* An application advertised by the card's PPSE directory. A card can carry
 * several — a debit application and a credit one, or a domestic scheme
 * alongside an international one. */
typedef struct {
    uint8_t aid[MONETA_AID_MAX];
    uint8_t aid_len;
    char label[MONETA_LABEL_MAX + 1];
    uint8_t priority;
} EmvApp;

typedef struct {
    /* --- identity, the part that matters --- */
    char pan[MONETA_PAN_DIGITS_MAX + 1];
    uint8_t pan_len;
    bool has_pan;
    bool pan_luhn_ok; /* a structurally valid card number, not a placeholder */
    bool pan_from_track2; /* came out of tag 57 rather than tag 5A */

    uint8_t exp_month; /* 1-12 */
    uint8_t exp_year; /* 2-digit, as printed on the card */
    bool has_expiry;

    char name[MONETA_NAME_MAX + 1];
    bool has_name; /* a real name — placeholders are rejected, see below */
    bool name_placeholder; /* card returned "UNKNOWN" or "/" */

    /* --- context --- */
    char label[MONETA_LABEL_MAX + 1]; /* "VISA DEBIT", "CREDIT" ... */
    bool has_label;
    uint8_t aid[MONETA_AID_MAX];
    uint8_t aid_len;
    EmvScheme scheme;

    uint8_t service_code[3];
    bool has_service_code;
    uint16_t country; /* 5F28, ISO 3166 numeric */
    bool has_country;
    uint16_t currency; /* 9F42, ISO 4217 numeric */
    bool has_currency;
    uint16_t atc; /* 9F36 application transaction counter */
    bool has_atc;
    uint8_t pin_try; /* 9F17 PIN tries left */
    bool has_pin_try;

    /* --- history --- */
    EmvLogEntry log[MONETA_LOG_MAX];
    uint8_t log_num;

    /* --- provenance --- */
    EmvApp apps[MONETA_APPS_MAX];
    uint8_t app_num;
    uint8_t records_read;
    uint8_t apdu_count; /* how many commands the "skimmer" had to send */
    bool is_demo;
} EmvCard;

void emv_card_reset(EmvCard* card);

/* Feed the parser one response body (SW1SW2 already stripped). Safe to call
 * with anything: a record, an FCI, a GPO response. Fields already filled by an
 * earlier, better source are not overwritten. Returns true if the buffer
 * contributed at least one new field. */
bool emv_card_ingest_tlv(EmvCard* card, const uint8_t* buf, size_t len);

/* Parse a PPSE / FCI directory into card->apps. */
bool emv_card_ingest_ppse(EmvCard* card, const uint8_t* buf, size_t len);

/* Parse one raw transaction-log record against the log format from tag 9F4F.
 * Log records are not TLV — they are a flat concatenation in the order the
 * format declares. */
bool emv_card_ingest_log_record(
    EmvCard* card,
    const uint8_t* fmt,
    size_t fmt_len,
    const uint8_t* rec,
    size_t rec_len);

/* Track 2 Equivalent Data (tag 57): PAN, 'D' separator, YYMM, service code. */
bool emv_card_parse_track2(EmvCard* card, const uint8_t* buf, size_t len);

/* --- presentation helpers, all NUL-terminating --- */

/* "4539 1488 0343 6467" — grouped for reading. */
void emv_card_format_pan(const EmvCard* card, char* out, size_t out_len);
/* "**** **** **** 6467" — what Moneta shows until you ask it not to. */
void emv_card_format_pan_masked(const EmvCard* card, char* out, size_t out_len);
/* "09/28", or "--/--" when the card kept it back. */
void emv_card_format_expiry(const EmvCard* card, char* out, size_t out_len);

const char* emv_scheme_name(EmvScheme scheme);
/* Numeric ISO country/currency codes to names, for the codes a card is
 * plausibly issued under. Falls back to the number itself. */
const char* emv_country_name(uint16_t code);
const char* emv_currency_name(uint16_t code);

/* Luhn (ISO/IEC 7812) check digit over an ASCII digit string. */
bool emv_luhn_check(const char* digits, size_t len);

/* Scheme from the AID, which is authoritative, falling back to the PAN's
 * issuer identification number when no AID was captured. */
EmvScheme emv_scheme_from_aid(const uint8_t* aid, size_t aid_len);
EmvScheme emv_scheme_from_pan(const char* pan, size_t pan_len);

#ifdef __cplusplus
}
#endif
