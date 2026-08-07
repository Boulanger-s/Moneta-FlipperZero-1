/* Host tests for Moneta's EMV engine.
 *
 * The parser and the grader are the product: everything else is a way of
 * showing them to someone. A screenshot cannot vouch for either, so both are
 * built for the host — with the sanitisers on — and checked on every push.
 *
 *     make -C test
 */

#include <stdio.h>
#include <string.h>

#include "../helpers/demo_card.h"
#include "../helpers/emv_card.h"
#include "../helpers/emv_tlv.h"
#include "../helpers/leak_grade.h"
#include "../helpers/report.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond, fmt, ...)                                             \
    do {                                                                  \
        checks++;                                                         \
        if(!(cond)) {                                                     \
            failures++;                                                   \
            printf("  FAIL %s:%d  " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        }                                                                 \
    } while(0)

#define CHECK_STR(actual, expected)                                       \
    do {                                                                  \
        checks++;                                                         \
        if(strcmp((actual), (expected)) != 0) {                           \
            failures++;                                                   \
            printf(                                                       \
                "  FAIL %s:%d  expected \"%s\", got \"%s\"\n",            \
                __FILE__,                                                 \
                __LINE__,                                                 \
                (expected),                                               \
                (actual));                                                \
        }                                                                 \
    } while(0)

#define CHECK_UINT(actual, expected)                                      \
    do {                                                                  \
        checks++;                                                         \
        unsigned long a_ = (unsigned long)(actual);                       \
        unsigned long e_ = (unsigned long)(expected);                     \
        if(a_ != e_) {                                                    \
            failures++;                                                   \
            printf(                                                       \
                "  FAIL %s:%d  expected %lu, got %lu\n", __FILE__, __LINE__, e_, a_); \
        }                                                                 \
    } while(0)

static void section(const char* name) {
    printf("%s\n", name);
}

/* ------------------------------------------------------------------ TLV */

static void test_tlv_basic(void) {
    section("TLV: flat sequences");

    /* 5A 08 <pan>  5F24 03 <yymmdd> */
    static const uint8_t buf[] = {0x5A, 0x08, 0x47, 0x61, 0x73, 0x90, 0x01, 0x01, 0x01,
                                  0x19, 0x5F, 0x24, 0x03, 0x28, 0x09, 0x30};

    EmvTlvIter it;
    EmvTlv tlv;
    emv_tlv_iter_init(&it, buf, sizeof(buf));

    CHECK(emv_tlv_iter_next(&it, &tlv), "first TLV missing");
    CHECK_UINT(tlv.tag, 0x5A);
    CHECK_UINT(tlv.length, 8);
    CHECK(!tlv.constructed, "5A must be primitive");

    CHECK(emv_tlv_iter_next(&it, &tlv), "second TLV missing");
    CHECK_UINT(tlv.tag, 0x5F24); /* two-byte tag packs big-endian */
    CHECK_UINT(tlv.length, 3);

    CHECK(!emv_tlv_iter_next(&it, &tlv), "iterator ran past the end");
    CHECK(!it.error, "clean walk must not set the error flag");
    CHECK(emv_tlv_is_valid_sequence(buf, sizeof(buf)), "sequence should validate");
}

static void test_tlv_nested(void) {
    section("TLV: descent into constructed templates");

    /* 6F -> A5 -> BF0C -> 61 -> {4F, 87}, the real PPSE shape. */
    static const uint8_t buf[] = {0x6F, 0x13, 0xA5, 0x11, 0xBF, 0x0C, 0x0E, 0x61, 0x0C,
                                  0x4F, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10,
                                  0x87, 0x01, 0x01};

    EmvTlv tlv;
    CHECK(emv_tlv_find(buf, sizeof(buf), 0x4F, &tlv), "4F not found at depth 4");
    CHECK_UINT(tlv.length, 7);
    CHECK_UINT(tlv.value[0], 0xA0);
    CHECK_UINT(tlv.value[6], 0x10);

    CHECK(emv_tlv_find(buf, sizeof(buf), 0xBF0C, &tlv), "three-nibble tag BF0C not found");
    CHECK(emv_tlv_find(buf, sizeof(buf), 0x87, &tlv), "87 not found");
    CHECK_UINT(emv_tlv_uint(&tlv), 1);

    CHECK(!emv_tlv_find(buf, sizeof(buf), 0x9F17, &tlv), "found a tag that is not there");
}

static void test_tlv_multiple(void) {
    section("TLV: repeated tags");

    /* Two 61 application templates side by side, as a dual-scheme card sends. */
    static const uint8_t buf[] = {0x61, 0x03, 0x4F, 0x01, 0xAA, 0x61, 0x03, 0x4F, 0x01, 0xBB};

    CHECK_UINT(emv_tlv_count(buf, sizeof(buf), 0x61), 2);
    CHECK_UINT(emv_tlv_count(buf, sizeof(buf), 0x4F), 2);

    EmvTlv tlv;
    CHECK(emv_tlv_find_nth(buf, sizeof(buf), 0x61, 0, &tlv), "first 61 missing");
    CHECK_UINT(tlv.value[2], 0xAA);
    CHECK(emv_tlv_find_nth(buf, sizeof(buf), 0x61, 1, &tlv), "second 61 missing");
    CHECK_UINT(tlv.value[2], 0xBB);
    CHECK(!emv_tlv_find_nth(buf, sizeof(buf), 0x61, 2, &tlv), "third 61 does not exist");
}

static void test_tlv_lengths(void) {
    section("TLV: long-form lengths");

    /* 81 80: one length byte carrying 128. */
    uint8_t buf[131];
    buf[0] = 0x70;
    buf[1] = 0x81;
    buf[2] = 0x80;
    memset(buf + 3, 0x11, 128);

    EmvTlvIter it;
    EmvTlv tlv;
    emv_tlv_iter_init(&it, buf, sizeof(buf));
    CHECK(emv_tlv_iter_next(&it, &tlv), "long-form TLV not parsed");
    CHECK_UINT(tlv.length, 128);
    CHECK(tlv.constructed, "70 is a constructed template");

    /* 82 00 82: two length bytes carrying 130. */
    uint8_t big[134];
    big[0] = 0x70;
    big[1] = 0x82;
    big[2] = 0x00;
    big[3] = 0x82;
    memset(big + 4, 0x22, 130);
    emv_tlv_iter_init(&it, big, sizeof(big));
    CHECK(emv_tlv_iter_next(&it, &tlv), "two-byte length not parsed");
    CHECK_UINT(tlv.length, 130);
}

static void test_tlv_malformed(void) {
    section("TLV: malformed input is refused, not trusted");

    /* Length runs past the end of the buffer. */
    static const uint8_t truncated[] = {0x5A, 0x08, 0x47, 0x61};
    CHECK(!emv_tlv_is_valid_sequence(truncated, sizeof(truncated)), "truncated TLV accepted");

    EmvTlvIter it;
    EmvTlv tlv;
    emv_tlv_iter_init(&it, truncated, sizeof(truncated));
    CHECK(!emv_tlv_iter_next(&it, &tlv), "truncated TLV returned a value");
    CHECK(it.error, "truncated TLV must raise the error flag");

    /* A tag whose continuation bytes never terminate. */
    static const uint8_t runaway[] = {0x9F, 0x80, 0x80, 0x80, 0x80, 0x80};
    emv_tlv_iter_init(&it, runaway, sizeof(runaway));
    CHECK(!emv_tlv_iter_next(&it, &tlv), "runaway tag accepted");

    /* Indefinite length (0x80) is not legal in EMV. */
    static const uint8_t indefinite[] = {0x70, 0x80, 0x00, 0x00};
    CHECK(
        !emv_tlv_is_valid_sequence(indefinite, sizeof(indefinite)),
        "indefinite length accepted");

    /* Padding between records must be skipped, not parsed. */
    static const uint8_t padded[] = {0x00, 0x00, 0x5A, 0x02, 0x12, 0x34, 0x00};
    CHECK(emv_tlv_find(padded, sizeof(padded), 0x5A, &tlv), "padding hid a real tag");
    CHECK_UINT(tlv.length, 2);

    /* Nothing at all. */
    CHECK(!emv_tlv_find(NULL, 0, 0x5A, &tlv), "NULL buffer accepted");
    CHECK(!emv_tlv_is_valid_sequence(NULL, 0), "NULL buffer validated");
    static const uint8_t empty[] = {0x00};
    CHECK(!emv_tlv_is_valid_sequence(empty, sizeof(empty)), "pure padding validated");
}

static void test_tlv_depth_guard(void) {
    section("TLV: recursion is bounded");

    /* Twenty nested constructed templates. The search must return rather than
     * ride the stack down. */
    uint8_t buf[64];
    size_t depth = 20;
    for(size_t i = 0; i < depth; i++) {
        buf[i * 2] = 0x70;
        buf[i * 2 + 1] = (uint8_t)((depth - i - 1) * 2 + 2);
    }
    buf[depth * 2] = 0x5A;
    buf[depth * 2 + 1] = 0x00;

    EmvTlv tlv;
    /* The tag is deeper than the limit, so not finding it is the correct
     * answer; the point of the test is that we get an answer at all. */
    (void)emv_tlv_find(buf, depth * 2 + 2, 0x5A, &tlv);
    CHECK(1, "depth-guarded search returned");
}

/* ----------------------------------------------------------------- Luhn */

static void test_luhn(void) {
    section("Luhn check digit");

    CHECK(emv_luhn_check("4761739001010119", 16), "known-good Visa PAN rejected");
    CHECK(emv_luhn_check("5413339000001513", 16), "known-good Mastercard PAN rejected");
    CHECK(emv_luhn_check("378282246310005", 15), "known-good Amex PAN rejected");
    CHECK(emv_luhn_check("30569309025904", 14), "known-good Diners PAN rejected");

    CHECK(!emv_luhn_check("4761739001010118", 16), "bad check digit accepted");
    CHECK(!emv_luhn_check("0000000000001234", 16), "masked placeholder accepted");
    CHECK(!emv_luhn_check("4", 1), "single digit accepted");
    CHECK(!emv_luhn_check("", 0), "empty string accepted");
    CHECK(!emv_luhn_check("47617390010101X9", 16), "non-digit accepted");
}

/* --------------------------------------------------------------- Track 2 */

static void test_track2(void) {
    section("Track 2 Equivalent Data");

    EmvCard card;
    emv_card_reset(&card);

    /* 4761739001010119 D 2809 201 0000993 F */
    static const uint8_t t2[] = {
        0x47, 0x61, 0x73, 0x90, 0x01, 0x01, 0x01, 0x19, 0xD2, 0x80, 0x92, 0x01, 0x00, 0x00, 0x99, 0x3F};

    CHECK(emv_card_parse_track2(&card, t2, sizeof(t2)), "track 2 not parsed");
    CHECK_STR(card.pan, "4761739001010119");
    CHECK_UINT(card.pan_len, 16);
    CHECK(card.pan_luhn_ok, "PAN failed Luhn");
    CHECK(card.pan_from_track2, "PAN source not recorded as track 2");
    CHECK(card.has_expiry, "expiry not parsed");
    CHECK_UINT(card.exp_year, 28);
    CHECK_UINT(card.exp_month, 9);
    CHECK(card.has_service_code, "service code not parsed");
    CHECK_UINT(card.service_code[0], 2);
    CHECK_UINT(card.service_code[1], 0);
    CHECK_UINT(card.service_code[2], 1);

    /* A 15-digit Amex PAN: odd digit count, so the separator lands mid-byte. */
    emv_card_reset(&card);
    static const uint8_t amex[] = {
        0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x5D, 0x27, 0x12, 0x20, 0x10, 0x00, 0x00, 0x0F};
    CHECK(emv_card_parse_track2(&card, amex, sizeof(amex)), "15-digit track 2 not parsed");
    CHECK_STR(card.pan, "378282246310005");
    CHECK_UINT(card.pan_len, 15);
    CHECK_UINT(card.exp_year, 27);
    CHECK_UINT(card.exp_month, 12);

    /* Month 13 is not a month. The rest of the field is still usable. */
    emv_card_reset(&card);
    static const uint8_t bad_month[] = {
        0x47, 0x61, 0x73, 0x90, 0x01, 0x01, 0x01, 0x19, 0xD2, 0x81, 0x32, 0x01, 0xFF};
    CHECK(emv_card_parse_track2(&card, bad_month, sizeof(bad_month)), "track 2 rejected");
    CHECK(card.has_pan, "PAN lost to a bad expiry");
    CHECK(!card.has_expiry, "month 13 accepted as an expiry");

    /* Too short to be a card number. */
    emv_card_reset(&card);
    static const uint8_t stub[] = {0x47, 0x61, 0xD2, 0x80, 0x9F};
    CHECK(!emv_card_parse_track2(&card, stub, sizeof(stub)), "7-digit PAN accepted");
    CHECK(!card.has_pan, "short PAN stored anyway");

    CHECK(!emv_card_parse_track2(&card, NULL, 0), "NULL track 2 accepted");
}

static void test_pan_tag(void) {
    section("Tag 5A (PAN) with nibble padding");

    EmvCard card;
    emv_card_reset(&card);

    /* 15-digit PAN in 8 bytes, the last nibble filled with 0xF. */
    static const uint8_t rec[] = {0x5A, 0x08, 0x37, 0x82, 0x82, 0x24,
                                  0x63, 0x10, 0x00, 0x5F};
    CHECK(emv_card_ingest_tlv(&card, rec, sizeof(rec)), "5A not ingested");
    CHECK_STR(card.pan, "378282246310005");
    CHECK_UINT(card.pan_len, 15);
    CHECK(!card.pan_from_track2, "5A wrongly marked as track 2");
    CHECK(card.pan_luhn_ok, "Amex PAN failed Luhn");
    CHECK_UINT(card.scheme, EmvSchemeAmex);
}

/* ---------------------------------------------------------------- scheme */

static void test_scheme_from_aid(void) {
    section("Scheme from AID");

    static const struct {
        const char* label;
        uint8_t aid[8];
        size_t len;
        EmvScheme want;
    } cases[] = {
        {"Visa", {0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10}, 7, EmvSchemeVisa},
        {"Mastercard", {0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10}, 7, EmvSchemeMastercard},
        {"Maestro", {0xA0, 0x00, 0x00, 0x00, 0x04, 0x30, 0x60}, 7, EmvSchemeMaestro},
        {"Amex", {0xA0, 0x00, 0x00, 0x00, 0x25, 0x01}, 6, EmvSchemeAmex},
        {"Discover", {0xA0, 0x00, 0x00, 0x01, 0x52, 0x30, 0x10}, 7, EmvSchemeDiscover},
        {"JCB", {0xA0, 0x00, 0x00, 0x00, 0x65, 0x10, 0x10}, 7, EmvSchemeJcb},
        {"UnionPay", {0xA0, 0x00, 0x00, 0x03, 0x33, 0x01, 0x01}, 7, EmvSchemeUnionPay},
        {"RuPay", {0xA0, 0x00, 0x00, 0x05, 0x24, 0x10, 0x10}, 7, EmvSchemeRuPay},
        {"Interac", {0xA0, 0x00, 0x00, 0x02, 0x77, 0x10, 0x10}, 7, EmvSchemeInterac},
        {"Mir", {0xA0, 0x00, 0x00, 0x06, 0x58, 0x10, 0x10}, 7, EmvSchemeMir},
        {"unknown RID", {0xA0, 0x00, 0x00, 0x09, 0x99, 0x10, 0x10}, 7, EmvSchemeUnknown},
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EmvScheme got = emv_scheme_from_aid(cases[i].aid, cases[i].len);
        checks++;
        if(got != cases[i].want) {
            failures++;
            printf("  FAIL %s: expected scheme %d, got %d\n", cases[i].label, cases[i].want, got);
        }
    }

    /* Too short to contain a RID. */
    static const uint8_t stub[] = {0xA0, 0x00, 0x00};
    CHECK_UINT(emv_scheme_from_aid(stub, sizeof(stub)), EmvSchemeUnknown);
    CHECK_UINT(emv_scheme_from_aid(NULL, 0), EmvSchemeUnknown);
}

static void test_scheme_from_pan(void) {
    section("Scheme from PAN prefix");

    static const struct {
        const char* pan;
        EmvScheme want;
    } cases[] = {
        {"4761739001010119", EmvSchemeVisa},
        {"4111111111111111", EmvSchemeVisa},
        {"5413339000001513", EmvSchemeMastercard},
        {"5555555555554444", EmvSchemeMastercard},
        {"2221000000000009", EmvSchemeMastercard}, /* the 2-series range */
        {"2720999999999999", EmvSchemeMastercard},
        {"378282246310005", EmvSchemeAmex},
        {"341111111111111", EmvSchemeAmex},
        {"6011111111111117", EmvSchemeDiscover},
        {"6445555555555555", EmvSchemeDiscover},
        {"6500000000000000", EmvSchemeDiscover},
        {"3530111333300000", EmvSchemeJcb},
        {"6221260000000000", EmvSchemeUnionPay},
        {"6521000000000000", EmvSchemeRuPay}, /* must beat Discover's 65 */
        {"6000000000000000", EmvSchemeRuPay},
        {"5080000000000000", EmvSchemeRuPay}, /* must beat Maestro's 50 */
        {"5018000000000009", EmvSchemeMaestro},
        {"6759000000000000", EmvSchemeMaestro},
        {"6767700000000000", EmvSchemeMaestro},
        {"30569309025904", EmvSchemeDiners},
        {"3600000000000", EmvSchemeDiners},
        {"2200000000000004", EmvSchemeMir},
        {"9999999999999999", EmvSchemeUnknown},
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        EmvScheme got = emv_scheme_from_pan(cases[i].pan, strlen(cases[i].pan));
        checks++;
        if(got != cases[i].want) {
            failures++;
            printf(
                "  FAIL %s: expected scheme %d, got %d\n", cases[i].pan, cases[i].want, got);
        }
    }

    CHECK_UINT(emv_scheme_from_pan("123", 3), EmvSchemeUnknown);
    CHECK_UINT(emv_scheme_from_pan(NULL, 0), EmvSchemeUnknown);

    /* Every scheme must have a printable name. */
    for(int s = EmvSchemeUnknown; s <= EmvSchemeMir; s++) {
        const char* n = emv_scheme_name((EmvScheme)s);
        CHECK(n != NULL && n[0] != '\0', "scheme %d has no name", s);
    }
}

/* ------------------------------------------------------------ formatting */

static void test_formatting(void) {
    section("Presentation: grouping, masking, expiry");

    EmvCard card;
    emv_card_reset(&card);

    static const uint8_t t2[] = {
        0x47, 0x61, 0x73, 0x90, 0x01, 0x01, 0x01, 0x19, 0xD2, 0x80, 0x92, 0x01, 0x00, 0x00, 0x99, 0x3F};
    emv_card_parse_track2(&card, t2, sizeof(t2));
    card.scheme = EmvSchemeVisa;

    char out[40];
    emv_card_format_pan(&card, out, sizeof(out));
    CHECK_STR(out, "4761 7390 0101 0119");

    emv_card_format_pan_masked(&card, out, sizeof(out));
    CHECK_STR(out, "**** **** **** 0119");

    emv_card_format_expiry(&card, out, sizeof(out));
    CHECK_STR(out, "09/28");

    /* Amex prints 4-6-5, not fours. */
    emv_card_reset(&card);
    static const uint8_t amex[] = {
        0x37, 0x82, 0x82, 0x24, 0x63, 0x10, 0x00, 0x5D, 0x27, 0x12, 0x20, 0x10, 0x00, 0x00, 0x0F};
    emv_card_parse_track2(&card, amex, sizeof(amex));
    card.scheme = EmvSchemeAmex;
    emv_card_format_pan(&card, out, sizeof(out));
    CHECK_STR(out, "3782 822463 10005");
    emv_card_format_pan_masked(&card, out, sizeof(out));
    CHECK_STR(out, "**** ****** *0005");

    /* No card, no expiry: say so rather than inventing one. */
    emv_card_reset(&card);
    emv_card_format_expiry(&card, out, sizeof(out));
    CHECK_STR(out, "--/--");
    emv_card_format_pan(&card, out, sizeof(out));
    CHECK_STR(out, "");

    /* A short buffer must truncate cleanly and stay terminated. */
    emv_card_parse_track2(&card, t2, sizeof(t2));
    char tiny[6];
    emv_card_format_pan(&card, tiny, sizeof(tiny));
    CHECK_UINT(strlen(tiny) < sizeof(tiny), 1);
    emv_card_format_expiry(&card, tiny, 3);
    CHECK_STR(tiny, "");
}

/* -------------------------------------------------------------- records */

static void test_name_placeholders(void) {
    section("Cardholder name: placeholders are not a leak");

    static const struct {
        const char* text;
        bool is_leak;
    } cases[] = {
        {"UNKNOWN", false},
        {"/", false},
        {" / ", false},
        {"CARDHOLDER", false},
        {"VALUED CUSTOMER", false},
        {"   ", false},
        {"R PATEL", true},
        {"SMITH/JOHN", true},
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t rec[64];
        size_t n = strlen(cases[i].text);
        rec[0] = 0x5F;
        rec[1] = 0x20;
        rec[2] = (uint8_t)n;
        memcpy(rec + 3, cases[i].text, n);

        EmvCard card;
        emv_card_reset(&card);
        emv_card_ingest_tlv(&card, rec, n + 3);

        checks++;
        if(card.has_name != cases[i].is_leak) {
            failures++;
            printf(
                "  FAIL name \"%s\": expected leak=%d, got %d\n",
                cases[i].text,
                cases[i].is_leak,
                card.has_name);
        }
        if(!cases[i].is_leak) {
            CHECK(card.name_placeholder, "placeholder \"%s\" not flagged", cases[i].text);
        }
    }
}

static void test_ppse(void) {
    section("PPSE directory");

    /* Two applications: Mastercard credit and Maestro. */
    static const uint8_t buf[] = {
        0x6F, 0x2E, 0x84, 0x0E, 0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44,
        0x44, 0x46, 0x30, 0x31, 0xA5, 0x1C, 0xBF, 0x0C, 0x19, 0x61, 0x0C, 0x4F, 0x07, 0xA0,
        0x00, 0x00, 0x00, 0x04, 0x10, 0x10, 0x87, 0x01, 0x01, 0x61, 0x09, 0x4F, 0x07, 0xA0,
        0x00, 0x00, 0x00, 0x04, 0x30, 0x60};

    EmvCard card;
    emv_card_reset(&card);
    CHECK(emv_card_ingest_ppse(&card, buf, sizeof(buf)), "PPSE not parsed");
    CHECK_UINT(card.app_num, 2);
    CHECK_UINT(card.apps[0].aid_len, 7);
    CHECK_UINT(card.apps[0].priority, 1);
    CHECK_UINT(card.apps[1].aid[6], 0x60);
    /* The first application becomes the card's identity. */
    CHECK_UINT(card.scheme, EmvSchemeMastercard);
    CHECK_UINT(emv_scheme_from_aid(card.apps[1].aid, card.apps[1].aid_len), EmvSchemeMaestro);

    emv_card_reset(&card);
    CHECK(!emv_card_ingest_ppse(&card, NULL, 0), "NULL PPSE accepted");
}

static void test_log_records(void) {
    section("Transaction log");

    /* Format 9F4F: date(3) amount(6) currency(2) merchant(10). */
    static const uint8_t fmt[] = {0x9A, 0x03, 0x9F, 0x02, 0x06, 0x5F, 0x2A, 0x02, 0x9F, 0x4E, 0x0A};
    /* 14 July 2026, 42.50, INR, "CAFE X". */
    static const uint8_t rec[] = {0x26, 0x07, 0x14, 0x00, 0x00, 0x00, 0x00, 0x42, 0x50, 0x03,
                                  0x56, 'C',  'A',  'F',  'E',  ' ',  'X',  ' ',  ' ',  ' ',
                                  ' '};

    EmvCard card;
    emv_card_reset(&card);
    CHECK(
        emv_card_ingest_log_record(&card, fmt, sizeof(fmt), rec, sizeof(rec)),
        "log record not parsed");
    CHECK_UINT(card.log_num, 1);
    CHECK(card.log[0].has_date, "log date missing");
    CHECK_UINT(card.log[0].year, 26);
    CHECK_UINT(card.log[0].month, 7);
    CHECK_UINT(card.log[0].day, 14);
    CHECK(card.log[0].has_amount, "log amount missing");
    CHECK_UINT(card.log[0].amount, 4250);
    CHECK(card.log[0].has_currency, "log currency missing");
    CHECK_UINT(card.log[0].currency, 356);
    CHECK(card.log[0].has_merchant, "merchant missing");
    CHECK_STR(card.log[0].merchant, "CAFE X");

    /* An unused log slot is all zeroes, and is not a transaction. */
    static const uint8_t blank[21] = {0};
    emv_card_reset(&card);
    CHECK(
        !emv_card_ingest_log_record(&card, fmt, sizeof(fmt), blank, sizeof(blank)),
        "empty log slot counted as a transaction");
    CHECK_UINT(card.log_num, 0);

    /* A record shorter than its own declared format must not read past it. */
    emv_card_reset(&card);
    static const uint8_t truncated[] = {0x26, 0x07, 0x14, 0x00};
    emv_card_ingest_log_record(&card, fmt, sizeof(fmt), truncated, sizeof(truncated));
    if(card.log_num > 0) {
        CHECK(!card.log[0].has_amount, "read an amount past the end of the record");
        CHECK(card.log[0].has_date, "date should still have parsed");
    }

    /* The log is bounded. */
    emv_card_reset(&card);
    for(int i = 0; i < MONETA_LOG_MAX + 5; i++) {
        emv_card_ingest_log_record(&card, fmt, sizeof(fmt), rec, sizeof(rec));
    }
    CHECK_UINT(card.log_num, MONETA_LOG_MAX);

    CHECK(!emv_card_ingest_log_record(&card, NULL, 0, rec, sizeof(rec)), "NULL format accepted");
}

/* --------------------------------------------------------------- grading */

static void test_grade_empty(void) {
    section("Grading: a card that gave nothing away");

    EmvCard card;
    emv_card_reset(&card);

    LeakReport r;
    leak_grade(&card, &r);
    CHECK_UINT(r.score, 0);
    CHECK_UINT(r.grade, LeakGradeAPlus);
    CHECK_UINT(r.leaked_count, 0);
    CHECK(!r.cnp_capable, "empty card marked spendable");

    leak_grade(NULL, &r);
    CHECK_UINT(r.grade, LeakGradeAPlus);
}

static void test_grade_fields(void) {
    section("Grading: what each field is worth");

    EmvCard card;
    LeakReport r;

    /* A PAN that fails its own check digit is a mask, not a number. */
    emv_card_reset(&card);
    card.has_pan = true;
    card.pan_len = 16;
    strcpy(card.pan, "4761739001010118");
    card.pan_luhn_ok = false;
    leak_grade(&card, &r);
    CHECK(!r.leaked[LeakFieldPan], "invalid PAN counted as leaked");
    CHECK_UINT(r.score, 0);

    card.pan_luhn_ok = true;
    leak_grade(&card, &r);
    CHECK(r.leaked[LeakFieldPan], "valid PAN not counted");
    CHECK_UINT(r.points[LeakFieldPan], 35);
    CHECK_UINT(r.score, 35);
    CHECK_UINT(r.grade, LeakGradeB);
    CHECK(!r.cnp_capable, "PAN alone is not enough to buy with");

    /* The number and the date together: the whole point of the app. */
    card.has_expiry = true;
    card.exp_month = 9;
    card.exp_year = 28;
    leak_grade(&card, &r);
    CHECK(r.cnp_capable, "PAN + expiry not flagged as spendable");
    CHECK_UINT(r.score, 55);
    CHECK_UINT(r.grade, LeakGradeD); /* the floor rule, not the band */

    /* A real name pushes it over. */
    card.has_name = true;
    strcpy(card.name, "R PATEL");
    leak_grade(&card, &r);
    CHECK_UINT(r.score, 75);
    CHECK_UINT(r.grade, LeakGradeD);

    /* Add the spending log with merchant names and it is an F. */
    card.log_num = 1;
    card.log[0].has_merchant = true;
    strcpy(card.log[0].merchant, "CAFE X");
    leak_grade(&card, &r);
    CHECK(r.leaked[LeakFieldLog], "log not counted");
    CHECK(r.leaked[LeakFieldMerchant], "merchant not counted");
    CHECK_UINT(r.score, 93);
    CHECK_UINT(r.grade, LeakGradeF);

    /* Everything at once must still land inside the scale. */
    card.has_service_code = true;
    card.has_atc = true;
    card.has_pin_try = true;
    card.has_country = true;
    card.has_currency = true;
    card.aid_len = 7;
    leak_grade(&card, &r);
    CHECK_UINT(r.score, 100);
    CHECK_UINT(r.grade, LeakGradeF);
    CHECK_UINT(r.leaked_count, 11);
}

static void test_grade_bands(void) {
    section("Grading: band boundaries");

    /* Drive the score directly through fields whose weights sum to each
     * boundary, and check the band flips exactly where documented. */
    static const struct {
        uint8_t score;
        LeakGrade want;
    } bands[] = {
        {0, LeakGradeAPlus},
        {1, LeakGradeA},
        {15, LeakGradeA},
        {16, LeakGradeB},
        {35, LeakGradeB},
        {36, LeakGradeC},
        {55, LeakGradeC},
        {56, LeakGradeD},
        {75, LeakGradeD},
        {76, LeakGradeF},
        {100, LeakGradeF},
    };

    for(size_t i = 0; i < sizeof(bands) / sizeof(bands[0]); i++) {
        /* Build a card scoring exactly bands[i].score out of the small fields
         * plus, where needed, the big ones — without setting PAN+expiry
         * together, so the floor rule does not mask the band under test. */
        EmvCard card;
        emv_card_reset(&card);
        uint8_t target = bands[i].score;

        if(target >= 35) {
            card.has_pan = true;
            card.pan_luhn_ok = true;
            card.pan_len = 16;
            strcpy(card.pan, "4761739001010119");
            target -= 35;
        }
        if(target >= 20) {
            card.has_name = true;
            strcpy(card.name, "R PATEL");
            target -= 20;
        }
        if(target >= 20) {
            card.has_expiry = true;
            card.exp_month = 1;
            card.exp_year = 30;
            target -= 20;
        }
        if(target >= 10) {
            card.log_num = 1;
            card.log[0].has_date = true;
            target -= 10;
        }
        if(target >= 8) {
            card.log_num = 1;
            card.log[0].has_merchant = true;
            target -= 8;
        }
        if(target >= 2) {
            card.has_service_code = true;
            target -= 2;
        }
        if(target >= 1) {
            card.has_atc = true;
            target -= 1;
        }
        if(target >= 1) {
            card.has_pin_try = true;
            target -= 1;
        }
        if(target >= 1) {
            card.has_country = true;
            target -= 1;
        }
        if(target >= 1) {
            card.has_currency = true;
            target -= 1;
        }
        if(target >= 1) {
            card.aid_len = 7;
            target -= 1;
        }

        LeakReport r;
        leak_grade(&card, &r);

        /* Only assert on the cases the field weights can hit exactly. */
        if(target != 0) continue;

        checks++;
        LeakGrade want = bands[i].want;
        if(r.cnp_capable && want < LeakGradeD) want = LeakGradeD;
        if(r.grade != want) {
            failures++;
            printf(
                "  FAIL score %u: expected grade %s, got %s\n",
                r.score,
                leak_grade_name(want),
                leak_grade_name(r.grade));
        }
        CHECK_UINT(r.score, bands[i].score);
    }
}

static void test_grade_strings(void) {
    section("Grading: every string is present");

    for(int g = LeakGradeAPlus; g <= LeakGradeF; g++) {
        const char* n = leak_grade_name((LeakGrade)g);
        const char* v = leak_grade_verdict((LeakGrade)g);
        CHECK(n != NULL && n[0] != '\0', "grade %d has no name", g);
        CHECK(v != NULL && strlen(v) > 10, "grade %d has no verdict", g);
    }

    for(int f = 0; f < LeakFieldCount; f++) {
        CHECK(strlen(leak_field_name((LeakField)f)) > 2, "field %d has no name", f);
        CHECK(strlen(leak_field_what((LeakField)f)) > 20, "field %d has no description", f);
        CHECK(strlen(leak_field_risk((LeakField)f)) > 20, "field %d has no risk text", f);
        CHECK(strlen(leak_field_defence((LeakField)f)) > 20, "field %d has no defence text", f);
    }

    for(int s = 0; s < LeakSafeCount; s++) {
        CHECK(strlen(leak_safe_name((LeakSafeFact)s)) > 2, "safe fact %d has no name", s);
        CHECK(strlen(leak_safe_text((LeakSafeFact)s)) > 20, "safe fact %d has no text", s);
    }
}

/* ------------------------------------------------------------ demo cards */

static void test_demo_cards(void) {
    section("Demo cards parse to the results they promise");

    CHECK_UINT(demo_card_count(), 3);

    EmvCard card;
    LeakReport r;

    /* 1. The ordinary high-street debit card. */
    CHECK(demo_card_load(0, &card), "demo card 0 failed to load");
    CHECK_UINT(card.scheme, EmvSchemeVisa);
    CHECK_STR(card.pan, "4761739001010119");
    CHECK(card.pan_luhn_ok, "demo PAN 0 fails Luhn");
    CHECK_UINT(card.exp_month, 9);
    CHECK_UINT(card.exp_year, 28);
    CHECK(!card.has_name, "demo card 0 must send UNKNOWN, not a name");
    CHECK(card.name_placeholder, "UNKNOWN not recognised as a placeholder");
    CHECK_STR(card.label, "VISA DEBIT");
    CHECK_UINT(card.country, 826);
    CHECK_UINT(card.currency, 826);
    CHECK_UINT(card.atc, 0x0271);
    CHECK_UINT(card.log_num, 0);
    CHECK_UINT(card.app_num, 1);
    CHECK(card.is_demo, "demo flag not set");
    leak_grade(&card, &r);
    CHECK(r.cnp_capable, "demo card 0 should be spendable");
    CHECK_UINT(r.grade, LeakGradeD);

    /* 2. The talkative credit card: name and a readable spending diary. */
    CHECK(demo_card_load(1, &card), "demo card 1 failed to load");
    CHECK_UINT(card.scheme, EmvSchemeMastercard);
    CHECK_STR(card.pan, "5413339000001513");
    CHECK(card.has_name, "demo card 1 must leak a name");
    CHECK_STR(card.name, "R PATEL");
    CHECK_UINT(card.app_num, 2); /* Mastercard + Maestro */
    CHECK_UINT(card.log_num, 4);
    CHECK_STR(card.log[0].merchant, "CAFE COFFEE DAY");
    CHECK_UINT(card.log[0].amount, 4250);
    CHECK_UINT(card.log[0].currency, 356);
    CHECK_UINT(card.log[1].month, 7);
    CHECK_UINT(card.log[1].day, 13);
    leak_grade(&card, &r);
    CHECK_UINT(r.grade, LeakGradeF);
    CHECK(r.leaked[LeakFieldMerchant], "demo card 1 merchant leak not graded");

    /* 3. The card that behaves. */
    CHECK(demo_card_load(2, &card), "demo card 2 failed to load");
    CHECK_UINT(card.scheme, EmvSchemeJcb);
    CHECK(!card.has_pan, "demo card 2 must not expose a PAN");
    CHECK(!card.has_expiry, "demo card 2 must not expose an expiry");
    CHECK_STR(card.label, "JCB CARD");
    leak_grade(&card, &r);
    CHECK(!r.cnp_capable, "demo card 2 must not be spendable");
    CHECK(r.grade <= LeakGradeA, "a card that gave up nothing should pass");

    /* Out of range must reset rather than leave a stale card on screen. */
    CHECK(!demo_card_load(99, &card), "out-of-range demo index accepted");
    CHECK(!card.has_pan, "failed load left data behind");

    for(size_t i = 0; i < demo_card_count(); i++) {
        CHECK(strlen(demo_card_title(i)) > 0, "demo %zu has no title", i);
        CHECK(strlen(demo_card_blurb(i)) > 0, "demo %zu has no blurb", i);
    }
}

/* ---------------------------------------------------------------- expiry */

static void test_expiry(void) {
    section("Expiry: valid through the whole of its month");

    EmvCard card;
    emv_card_reset(&card);
    card.has_expiry = true;
    card.exp_year = 28; /* 2028 */
    card.exp_month = 9;

    CHECK(!emv_card_is_expired(&card, 2026, 8), "a 2028 card is not expired in 2026");
    CHECK(!emv_card_is_expired(&card, 2028, 1), "not expired earlier in the same year");
    /* A card marked 09/28 is good until the end of September 2028. */
    CHECK(!emv_card_is_expired(&card, 2028, 9), "expired during its own expiry month");
    CHECK(emv_card_is_expired(&card, 2028, 10), "not expired the month after");
    CHECK(emv_card_is_expired(&card, 2029, 1), "not expired the following year");

    /* No readable expiry is never reported as expired — that would be an
     * invention, and the screen would be stating a fact the card never gave. */
    emv_card_reset(&card);
    CHECK(!emv_card_is_expired(&card, 2030, 6), "a card with no expiry was called expired");
    CHECK(!emv_card_is_expired(NULL, 2030, 6), "NULL card was called expired");

    /* A nonsense clock must not produce a verdict either. */
    card.has_expiry = true;
    card.exp_year = 20;
    card.exp_month = 1;
    CHECK(!emv_card_is_expired(&card, 2026, 0), "month 0 accepted as a clock");
    CHECK(!emv_card_is_expired(&card, 2026, 13), "month 13 accepted as a clock");
}

/* ---------------------------------------------------------------- report */

static void test_report_redaction(void) {
    section("Report: the full card number never reaches the file");

    EmvCard card;
    LeakReport r;
    char buf[MONETA_REPORT_MAX];

    for(size_t i = 0; i < demo_card_count(); i++) {
        CHECK(demo_card_load(i, &card), "demo card %zu failed to load", i);
        leak_grade(&card, &r);

        size_t n = report_build(&card, &r, "1.1", "2026-08-07 02:14", buf, sizeof(buf));
        CHECK(n > 0, "demo %zu produced an empty report", i);
        CHECK(n < sizeof(buf), "demo %zu report was not terminated in bounds", i);
        CHECK(strlen(buf) == n, "demo %zu reported a length that is not the string", i);

        if(card.has_pan) {
            /* The whole promise of the feature, checked rather than asserted. */
            CHECK(
                strstr(buf, card.pan) == NULL,
                "demo %zu leaked the full PAN into the report",
                i);

            /* The last four are supposed to be there — a report that redacted
             * everything would pass the test above and be useless. */
            const char* last4 = card.pan + card.pan_len - 4;
            CHECK(strstr(buf, last4) != NULL, "demo %zu dropped the last four digits", i);

            /* Nor may any long run of the PAN survive: catches a future edit
             * that masks only the first few digits. */
            char chunk[9];
            for(size_t off = 0; off + 8 <= card.pan_len; off++) {
                memcpy(chunk, card.pan + off, 8);
                chunk[8] = '\0';
                CHECK(
                    strstr(buf, chunk) == NULL,
                    "demo %zu leaked 8 consecutive PAN digits at offset %zu",
                    i,
                    off);
            }
        }

        CHECK(strstr(buf, "grade") != NULL, "demo %zu report has no grade line", i);
    }

    /* A card that leaked a name must not have the name redacted away — the
     * report is for the cardholder, and hiding their own name helps nobody. */
    CHECK(demo_card_load(1, &card), "demo card 1 failed to load");
    leak_grade(&card, &r);
    report_build(&card, &r, "1.1", NULL, buf, sizeof(buf));
    CHECK(strstr(buf, "LEAKED") != NULL, "a leaked cardholder name was not reported");

    /* Truncation must stay in bounds and stay terminated. */
    char tiny[64];
    size_t n = report_build(&card, &r, "1.1", "stamp", tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny), "small buffer overran");
    CHECK(strlen(tiny) < sizeof(tiny), "small buffer left an unterminated string");

    char one[1];
    CHECK_UINT(report_build(&card, &r, "1.1", NULL, one, sizeof(one)), 0);
    CHECK_STR(one, "");

    CHECK_UINT(report_build(NULL, &r, "1.1", NULL, buf, sizeof(buf)), 0);
    CHECK_UINT(report_build(&card, NULL, "1.1", NULL, buf, sizeof(buf)), 0);
    CHECK_UINT(report_build(&card, &r, "1.1", NULL, NULL, 0), 0);
}

/* ------------------------------------------------------------------ main */

int main(void) {
    printf("Moneta EMV engine — host tests\n\n");

    test_tlv_basic();
    test_tlv_nested();
    test_tlv_multiple();
    test_tlv_lengths();
    test_tlv_malformed();
    test_tlv_depth_guard();
    test_luhn();
    test_track2();
    test_pan_tag();
    test_scheme_from_aid();
    test_scheme_from_pan();
    test_formatting();
    test_name_placeholders();
    test_ppse();
    test_log_records();
    test_grade_empty();
    test_grade_fields();
    test_grade_bands();
    test_grade_strings();
    test_demo_cards();
    test_expiry();
    test_report_redaction();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
