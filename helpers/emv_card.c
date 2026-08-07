#include "emv_card.h"

#include <string.h>

/* ------------------------------------------------------------------ tags */

#define TAG_TRACK2 0x57u /* Track 2 Equivalent Data — PAN + expiry + service code */
#define TAG_PAN 0x5Au /* Application PAN */
#define TAG_NAME 0x5F20u /* Cardholder Name */
#define TAG_EXPIRY 0x5F24u /* Application Expiration Date, YYMMDD */
#define TAG_COUNTRY 0x5F28u /* Issuer Country Code */
#define TAG_LABEL 0x50u /* Application Label */
#define TAG_PREF_NAME 0x9F12u /* Application Preferred Name */
#define TAG_ADF_NAME 0x4Fu /* ADF Name (the AID) */
#define TAG_DF_NAME 0x84u /* DF Name (the AID, in an FCI) */
#define TAG_PRIORITY 0x87u /* Application Priority Indicator */
#define TAG_APP_TEMPLATE 0x61u /* Application Template, inside the PPSE */
#define TAG_CURRENCY 0x9F42u /* Application Currency Code */
#define TAG_ATC 0x9F36u /* Application Transaction Counter */
#define TAG_PIN_TRY 0x9F17u /* PIN Try Counter */
#define TAG_LOG_AMOUNT 0x9F02u
#define TAG_LOG_CURRENCY 0x5F2Au
#define TAG_LOG_COUNTRY 0x9F1Au
#define TAG_LOG_DATE 0x9Au
#define TAG_LOG_MERCHANT 0x9F4Eu

/* --------------------------------------------------------------- helpers */

static void str_copy_trimmed(char* dst, size_t dst_size, const uint8_t* src, size_t src_len) {
    /* Trim leading and trailing spaces, drop anything unprintable. Cards pad
     * text fields with 0x20 and occasionally with 0x00. */
    size_t start = 0;
    while(start < src_len && (src[start] == ' ' || src[start] == '\0')) start++;

    size_t end = src_len;
    while(end > start && (src[end - 1] == ' ' || src[end - 1] == '\0')) end--;

    size_t out = 0;
    for(size_t i = start; i < end && out + 1 < dst_size; i++) {
        uint8_t c = src[i];
        dst[out++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    dst[out] = '\0';
}

/* A cardholder name field that carries no cardholder. Issuers that care about
 * privacy fill it with one of these rather than leaving the tag out. */
static bool name_is_placeholder(const char* name) {
    if(name[0] == '\0') return true;

    /* Strip the separator and see whether anything is left. Track-1 style
     * names are "SURNAME/FIRSTNAME", so "/" alone means both halves empty. */
    bool has_letter = false;
    for(const char* p = name; *p; p++) {
        if(*p != '/' && *p != ' ' && *p != '.') has_letter = true;
    }
    if(!has_letter) return true;

    static const char* const placeholders[] = {
        "UNKNOWN",
        "UNKNOWN/",
        "/UNKNOWN",
        "CARDHOLDER",
        "CARDHOLDER/",
        "VALUED CUSTOMER",
        "NOT AVAILABLE",
        "CUSTOMER",
    };
    for(size_t i = 0; i < sizeof(placeholders) / sizeof(placeholders[0]); i++) {
        if(strcmp(name, placeholders[i]) == 0) return true;
    }
    return false;
}

/* Two BCD nibbles per byte into an integer. Returns false on a non-decimal
 * nibble, which is how a padded or corrupt field announces itself. */
static bool bcd_to_uint(const uint8_t* buf, size_t len, uint32_t* out) {
    uint32_t v = 0;
    for(size_t i = 0; i < len; i++) {
        uint8_t hi = buf[i] >> 4;
        uint8_t lo = buf[i] & 0x0Fu;
        if(hi > 9 || lo > 9) return false;
        /* Saturate rather than wrap: a 6-byte n12 amount can exceed uint32,
         * and a wrapped number would be a lie on screen. */
        if(v > (UINT32_MAX - lo) / 100) {
            *out = UINT32_MAX;
            return true;
        }
        v = v * 100 + hi * 10 + lo;
    }
    *out = v;
    return true;
}

bool emv_luhn_check(const char* digits, size_t len) {
    if(len < 2) return false;

    int sum = 0;
    bool double_it = false;
    for(size_t i = len; i > 0; i--) {
        char c = digits[i - 1];
        if(c < '0' || c > '9') return false;
        int d = c - '0';
        if(double_it) {
            d *= 2;
            if(d > 9) d -= 9;
        }
        sum += d;
        double_it = !double_it;
    }
    return (sum % 10) == 0;
}

/* ------------------------------------------------------------- reset */

void emv_card_reset(EmvCard* card) {
    if(card == NULL) return;
    memset(card, 0, sizeof(*card));
}

/* ------------------------------------------------------------- scheme */

/* The first five bytes of an AID are the Registered Application Provider
 * Identifier — the scheme itself. Everything after that is the product. */
EmvScheme emv_scheme_from_aid(const uint8_t* aid, size_t aid_len) {
    if(aid == NULL || aid_len < 5) return EmvSchemeUnknown;

    static const struct {
        uint8_t rid[5];
        EmvScheme scheme;
    } rids[] = {
        {{0xA0, 0x00, 0x00, 0x00, 0x03}, EmvSchemeVisa},
        {{0xA0, 0x00, 0x00, 0x00, 0x04}, EmvSchemeMastercard},
        {{0xA0, 0x00, 0x00, 0x00, 0x25}, EmvSchemeAmex},
        {{0xA0, 0x00, 0x00, 0x01, 0x52}, EmvSchemeDiscover},
        {{0xA0, 0x00, 0x00, 0x03, 0x24}, EmvSchemeDiscover},
        {{0xA0, 0x00, 0x00, 0x00, 0x65}, EmvSchemeJcb},
        {{0xA0, 0x00, 0x00, 0x03, 0x33}, EmvSchemeUnionPay},
        {{0xA0, 0x00, 0x00, 0x05, 0x24}, EmvSchemeRuPay},
        {{0xA0, 0x00, 0x00, 0x02, 0x77}, EmvSchemeInterac},
        {{0xA0, 0x00, 0x00, 0x06, 0x58}, EmvSchemeMir},
    };

    for(size_t i = 0; i < sizeof(rids) / sizeof(rids[0]); i++) {
        if(memcmp(aid, rids[i].rid, 5) == 0) {
            /* A0000000043060 is Maestro, not Mastercard credit — the product
             * bytes are the only thing that separates them. */
            if(rids[i].scheme == EmvSchemeMastercard && aid_len >= 7 && aid[5] == 0x30 &&
               aid[6] == 0x60) {
                return EmvSchemeMaestro;
            }
            return rids[i].scheme;
        }
    }
    return EmvSchemeUnknown;
}

/* Numeric prefix of the PAN, `n` digits wide. */
static int pan_prefix(const char* pan, size_t pan_len, size_t n) {
    if(pan_len < n) return -1;
    int v = 0;
    for(size_t i = 0; i < n; i++) {
        if(pan[i] < '0' || pan[i] > '9') return -1;
        v = v * 10 + (pan[i] - '0');
    }
    return v;
}

EmvScheme emv_scheme_from_pan(const char* pan, size_t pan_len) {
    if(pan == NULL || pan_len < 4) return EmvSchemeUnknown;

    int p1 = pan_prefix(pan, pan_len, 1);
    int p2 = pan_prefix(pan, pan_len, 2);
    int p3 = pan_prefix(pan, pan_len, 3);
    int p4 = pan_prefix(pan, pan_len, 4);
    int p6 = pan_prefix(pan, pan_len, 6);

    if(p1 == 4) return EmvSchemeVisa;
    if(p2 == 34 || p2 == 37) return EmvSchemeAmex;
    if(p4 >= 2221 && p4 <= 2720) return EmvSchemeMastercard;
    if(p2 >= 51 && p2 <= 55) return EmvSchemeMastercard;
    if(p4 >= 2200 && p4 <= 2204) return EmvSchemeMir;
    if(p4 >= 3528 && p4 <= 3589) return EmvSchemeJcb;
    if(p2 == 62) return EmvSchemeUnionPay;

    /* 6xxxxx is contested ground: Discover, Maestro and RuPay all live there,
     * so the longer prefixes have to be tested before the shorter ones. */
    if(p4 == 6011) return EmvSchemeDiscover;
    if(p6 == 676770 || p6 == 676774) return EmvSchemeMaestro;
    if(p4 == 6759) return EmvSchemeMaestro;
    if(p4 == 6521 || p4 == 6522) return EmvSchemeRuPay;
    if(p3 >= 644 && p3 <= 649) return EmvSchemeDiscover;
    if(p2 == 65) return EmvSchemeDiscover;
    if(p2 == 60) return EmvSchemeRuPay;
    if(p3 == 508) return EmvSchemeRuPay;
    if(p2 == 50 || (p2 >= 56 && p2 <= 58)) return EmvSchemeMaestro;

    if(p4 == 3095 || p2 == 36 || p2 == 38 || p2 == 39) return EmvSchemeDiners;
    if(p3 >= 300 && p3 <= 305) return EmvSchemeDiners;

    return EmvSchemeUnknown;
}

const char* emv_scheme_name(EmvScheme scheme) {
    switch(scheme) {
    case EmvSchemeVisa:
        return "Visa";
    case EmvSchemeMastercard:
        return "Mastercard";
    case EmvSchemeMaestro:
        return "Maestro";
    case EmvSchemeAmex:
        return "Amex";
    case EmvSchemeDiscover:
        return "Discover";
    case EmvSchemeJcb:
        return "JCB";
    case EmvSchemeUnionPay:
        return "UnionPay";
    case EmvSchemeRuPay:
        return "RuPay";
    case EmvSchemeDiners:
        return "Diners Club";
    case EmvSchemeInterac:
        return "Interac";
    case EmvSchemeMir:
        return "Mir";
    default:
        return "Payment card";
    }
}

/* --------------------------------------------------- country / currency */

const char* emv_country_name(uint16_t code) {
    static const struct {
        uint16_t code;
        const char* name;
    } table[] = {
        {36, "Australia"},   {76, "Brazil"},        {124, "Canada"},   {156, "China"},
        {203, "Czechia"},    {208, "Denmark"},      {246, "Finland"},  {250, "France"},
        {276, "Germany"},    {344, "Hong Kong"},    {356, "India"},    {372, "Ireland"},
        {376, "Israel"},     {380, "Italy"},        {392, "Japan"},    {410, "South Korea"},
        {458, "Malaysia"},   {484, "Mexico"},       {528, "Netherlands"}, {554, "New Zealand"},
        {578, "Norway"},     {608, "Philippines"},  {616, "Poland"},   {620, "Portugal"},
        {643, "Russia"},     {682, "Saudi Arabia"}, {702, "Singapore"}, {710, "South Africa"},
        {724, "Spain"},      {752, "Sweden"},       {756, "Switzerland"}, {764, "Thailand"},
        {784, "UAE"},        {792, "Turkey"},       {804, "Ukraine"},  {826, "UK"},
        {840, "USA"},        {860, "Uzbekistan"},   {704, "Vietnam"},
    };
    for(size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if(table[i].code == code) return table[i].name;
    }
    return NULL;
}

const char* emv_currency_name(uint16_t code) {
    static const struct {
        uint16_t code;
        const char* name;
    } table[] = {
        {36, "AUD"},  {76, "BRL"},  {124, "CAD"}, {156, "CNY"}, {203, "CZK"}, {208, "DKK"},
        {344, "HKD"}, {356, "INR"}, {376, "ILS"}, {392, "JPY"}, {410, "KRW"}, {458, "MYR"},
        {484, "MXN"}, {554, "NZD"}, {578, "NOK"}, {608, "PHP"}, {643, "RUB"}, {682, "SAR"},
        {702, "SGD"}, {710, "ZAR"}, {752, "SEK"}, {756, "CHF"}, {764, "THB"}, {784, "AED"},
        {792, "TRY"}, {826, "GBP"}, {840, "USD"}, {978, "EUR"}, {985, "PLN"},
    };
    for(size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if(table[i].code == code) return table[i].name;
    }
    return NULL;
}

/* ------------------------------------------------------------- track 2 */

bool emv_card_parse_track2(EmvCard* card, const uint8_t* buf, size_t len) {
    if(card == NULL || buf == NULL || len == 0) return false;

    /* Track 2 is a nibble stream: PAN digits, 0xD, YYMM, three service-code
     * digits, then discretionary data, padded to a byte boundary with 0xF. */
    char pan[MONETA_PAN_DIGITS_MAX + 1];
    size_t pan_len = 0;
    size_t nibble = 0;
    size_t total_nibbles = len * 2;
    bool found_sep = false;

    for(; nibble < total_nibbles; nibble++) {
        uint8_t n = (nibble & 1) ? (buf[nibble / 2] & 0x0Fu) : (buf[nibble / 2] >> 4);
        if(n == 0x0D) {
            found_sep = true;
            nibble++;
            break;
        }
        if(n > 9) break; /* 0xF padding, or garbage — either way the PAN ended */
        if(pan_len >= MONETA_PAN_DIGITS_MAX) return false;
        pan[pan_len++] = (char)('0' + n);
    }

    if(pan_len < 8) return false; /* not a card number */
    pan[pan_len] = '\0';

    if(!card->has_pan) {
        memcpy(card->pan, pan, pan_len + 1);
        card->pan_len = (uint8_t)pan_len;
        card->has_pan = true;
        card->pan_from_track2 = true;
        card->pan_luhn_ok = emv_luhn_check(pan, pan_len);
    }

    if(!found_sep) return true; /* PAN only; no expiry follows */

    /* YYMM */
    if(!card->has_expiry && nibble + 4 <= total_nibbles) {
        uint8_t d[4];
        bool ok = true;
        for(size_t i = 0; i < 4; i++) {
            size_t k = nibble + i;
            d[i] = (k & 1) ? (buf[k / 2] & 0x0Fu) : (buf[k / 2] >> 4);
            if(d[i] > 9) ok = false;
        }
        if(ok) {
            uint8_t month = d[2] * 10 + d[3];
            if(month >= 1 && month <= 12) {
                card->exp_year = d[0] * 10 + d[1];
                card->exp_month = month;
                card->has_expiry = true;
            }
        }
    }
    nibble += 4;

    /* Service code */
    if(!card->has_service_code && nibble + 3 <= total_nibbles) {
        uint8_t d[3];
        bool ok = true;
        for(size_t i = 0; i < 3; i++) {
            size_t k = nibble + i;
            d[i] = (k & 1) ? (buf[k / 2] & 0x0Fu) : (buf[k / 2] >> 4);
            if(d[i] > 9) ok = false;
        }
        if(ok) {
            card->service_code[0] = d[0];
            card->service_code[1] = d[1];
            card->service_code[2] = d[2];
            card->has_service_code = true;
        }
    }

    return true;
}

/* --------------------------------------------------------------- ingest */

/* Tag 5A: PAN as BCD, right-padded with 0xF nibbles. */
static bool ingest_pan_tag(EmvCard* card, const uint8_t* buf, size_t len) {
    if(card->has_pan) return false;

    char pan[MONETA_PAN_DIGITS_MAX + 1];
    size_t pan_len = 0;

    for(size_t i = 0; i < len * 2; i++) {
        uint8_t n = (i & 1) ? (buf[i / 2] & 0x0Fu) : (buf[i / 2] >> 4);
        if(n > 9) break;
        if(pan_len >= MONETA_PAN_DIGITS_MAX) return false;
        pan[pan_len++] = (char)('0' + n);
    }
    if(pan_len < 8) return false;

    pan[pan_len] = '\0';
    memcpy(card->pan, pan, pan_len + 1);
    card->pan_len = (uint8_t)pan_len;
    card->has_pan = true;
    card->pan_from_track2 = false;
    card->pan_luhn_ok = emv_luhn_check(pan, pan_len);
    return true;
}

bool emv_card_ingest_tlv(EmvCard* card, const uint8_t* buf, size_t len) {
    if(card == NULL || buf == NULL || len == 0) return false;

    bool gained = false;
    EmvTlv tlv;

    /* Track 2 first: it carries the PAN, the expiry and the service code in
     * one field, and it is the field a real skimmer targets. */
    if(!card->has_pan && emv_tlv_find(buf, len, TAG_TRACK2, &tlv)) {
        if(emv_card_parse_track2(card, tlv.value, tlv.length)) gained = true;
    }
    if(!card->has_pan && emv_tlv_find(buf, len, TAG_PAN, &tlv)) {
        if(ingest_pan_tag(card, tlv.value, tlv.length)) gained = true;
    }

    /* 5F24 is YYMMDD; the day is the last day of the month and is not printed
     * on the card, so we keep only what the cardholder would recognise. */
    if(!card->has_expiry && emv_tlv_find(buf, len, TAG_EXPIRY, &tlv) && tlv.length >= 2) {
        uint32_t yy = 0, mm = 0;
        if(bcd_to_uint(tlv.value, 1, &yy) && bcd_to_uint(tlv.value + 1, 1, &mm) && mm >= 1 &&
           mm <= 12) {
            card->exp_year = (uint8_t)yy;
            card->exp_month = (uint8_t)mm;
            card->has_expiry = true;
            gained = true;
        }
    }

    if(!card->has_name && !card->name_placeholder &&
       emv_tlv_find(buf, len, TAG_NAME, &tlv) && tlv.length > 0) {
        char name[MONETA_NAME_MAX + 1];
        str_copy_trimmed(name, sizeof(name), tlv.value, tlv.length);
        if(name_is_placeholder(name)) {
            card->name_placeholder = true;
        } else {
            memcpy(card->name, name, sizeof(name));
            card->has_name = true;
        }
        gained = true;
    }

    if(!card->has_label) {
        /* 9F12 is the issuer's preferred, localised name; 50 is the generic
         * label. Prefer the specific one — but a 9F12 that trims away to
         * nothing must not stop us falling back to 50, which is what happens
         * on cards that pad the preferred name with spaces. */
        if(emv_tlv_find(buf, len, TAG_PREF_NAME, &tlv) && tlv.length > 0) {
            str_copy_trimmed(card->label, sizeof(card->label), tlv.value, tlv.length);
            card->has_label = card->label[0] != '\0';
        }
        if(!card->has_label && emv_tlv_find(buf, len, TAG_LABEL, &tlv) && tlv.length > 0) {
            str_copy_trimmed(card->label, sizeof(card->label), tlv.value, tlv.length);
            card->has_label = card->label[0] != '\0';
        }
        gained = gained || card->has_label;
    }

    if(!card->has_country && emv_tlv_find(buf, len, TAG_COUNTRY, &tlv) && tlv.length >= 2) {
        uint32_t v = 0;
        if(bcd_to_uint(tlv.value, 2, &v)) {
            card->country = (uint16_t)v;
            card->has_country = true;
            gained = true;
        }
    }

    if(!card->has_currency && emv_tlv_find(buf, len, TAG_CURRENCY, &tlv) && tlv.length >= 2) {
        uint32_t v = 0;
        if(bcd_to_uint(tlv.value, 2, &v)) {
            card->currency = (uint16_t)v;
            card->has_currency = true;
            gained = true;
        }
    }

    /* The ATC is a plain binary counter, not BCD. */
    if(!card->has_atc && emv_tlv_find(buf, len, TAG_ATC, &tlv) && tlv.length >= 2) {
        card->atc = (uint16_t)((tlv.value[0] << 8) | tlv.value[1]);
        card->has_atc = true;
        gained = true;
    }

    if(!card->has_pin_try && emv_tlv_find(buf, len, TAG_PIN_TRY, &tlv) && tlv.length >= 1) {
        card->pin_try = tlv.value[0];
        card->has_pin_try = true;
        gained = true;
    }

    if(card->aid_len == 0 && emv_tlv_find(buf, len, TAG_DF_NAME, &tlv) && tlv.length > 0 &&
       tlv.length <= MONETA_AID_MAX) {
        memcpy(card->aid, tlv.value, tlv.length);
        card->aid_len = (uint8_t)tlv.length;
        gained = true;
    }

    /* Recompute the scheme from whatever we now hold. The AID wins because it
     * is the issuer's own declaration; the PAN prefix is an inference. */
    EmvScheme s = emv_scheme_from_aid(card->aid, card->aid_len);
    if(s == EmvSchemeUnknown && card->has_pan) {
        s = emv_scheme_from_pan(card->pan, card->pan_len);
    }
    if(s != EmvSchemeUnknown) card->scheme = s;

    return gained;
}

bool emv_card_ingest_ppse(EmvCard* card, const uint8_t* buf, size_t len) {
    if(card == NULL || buf == NULL || len == 0) return false;

    bool gained = false;
    for(size_t i = 0; i < MONETA_APPS_MAX; i++) {
        EmvTlv tmpl;
        if(!emv_tlv_find_nth(buf, len, TAG_APP_TEMPLATE, i, &tmpl)) break;
        if(card->app_num >= MONETA_APPS_MAX) break;

        EmvApp* app = &card->apps[card->app_num];
        memset(app, 0, sizeof(*app));

        EmvTlv tlv;
        if(emv_tlv_find(tmpl.value, tmpl.length, TAG_ADF_NAME, &tlv) && tlv.length > 0 &&
           tlv.length <= MONETA_AID_MAX) {
            memcpy(app->aid, tlv.value, tlv.length);
            app->aid_len = (uint8_t)tlv.length;
        } else {
            continue; /* an application template without an AID is unusable */
        }
        if(emv_tlv_find(tmpl.value, tmpl.length, TAG_LABEL, &tlv) && tlv.length > 0) {
            str_copy_trimmed(app->label, sizeof(app->label), tlv.value, tlv.length);
        }
        if(emv_tlv_find(tmpl.value, tmpl.length, TAG_PRIORITY, &tlv) && tlv.length >= 1) {
            app->priority = tlv.value[0] & 0x0Fu;
        }

        card->app_num++;
        gained = true;
    }

    if(gained && card->aid_len == 0) {
        memcpy(card->aid, card->apps[0].aid, card->apps[0].aid_len);
        card->aid_len = card->apps[0].aid_len;
        EmvScheme s = emv_scheme_from_aid(card->aid, card->aid_len);
        if(s != EmvSchemeUnknown) card->scheme = s;
    }
    return gained;
}

/* ----------------------------------------------------------- log records */

/* A log record is not TLV. Tag 9F4F declares a Data Object List — pairs of
 * (tag, length) — and the record is those fields concatenated in that order,
 * with no tags and no delimiters. Reading it means walking the format and the
 * record in lockstep. */
bool emv_card_ingest_log_record(
    EmvCard* card,
    const uint8_t* fmt,
    size_t fmt_len,
    const uint8_t* rec,
    size_t rec_len) {
    if(card == NULL || fmt == NULL || rec == NULL) return false;
    if(card->log_num >= MONETA_LOG_MAX) return false;
    if(fmt_len == 0 || rec_len == 0) return false;

    EmvLogEntry entry;
    memset(&entry, 0, sizeof(entry));

    size_t fpos = 0;
    size_t rpos = 0;
    bool any = false;

    while(fpos < fmt_len) {
        /* Tag, same encoding as BER but always followed by a single length
         * byte — DOL entries never use the long form. */
        EmvTag tag = fmt[fpos];
        fpos++;
        if((tag & 0x1Fu) == 0x1Fu) {
            size_t extra = 0;
            for(;;) {
                if(fpos >= fmt_len) return false;
                uint8_t b = fmt[fpos];
                fpos++;
                tag = (tag << 8) | b;
                if((b & 0x80u) == 0) break;
                if(++extra >= 3) return false;
            }
        }
        if(fpos >= fmt_len) break;
        size_t flen = fmt[fpos];
        fpos++;

        if(flen == 0) continue;
        if(rpos + flen > rec_len) break; /* record shorter than its own format */

        const uint8_t* v = rec + rpos;
        uint32_t num = 0;

        switch(tag) {
        case TAG_LOG_AMOUNT:
            if(bcd_to_uint(v, flen, &num)) {
                entry.amount = num;
                entry.has_amount = true;
                any = true;
            }
            break;
        case TAG_LOG_CURRENCY:
            if(flen >= 2 && bcd_to_uint(v, 2, &num)) {
                entry.currency = (uint16_t)num;
                entry.has_currency = true;
                any = true;
            }
            break;
        case TAG_LOG_COUNTRY:
            if(flen >= 2 && bcd_to_uint(v, 2, &num)) {
                entry.country = (uint16_t)num;
                entry.has_country = true;
                any = true;
            }
            break;
        case TAG_LOG_DATE:
            if(flen >= 3) {
                uint32_t yy = 0, mm = 0, dd = 0;
                if(bcd_to_uint(v, 1, &yy) && bcd_to_uint(v + 1, 1, &mm) &&
                   bcd_to_uint(v + 2, 1, &dd) && mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) {
                    entry.year = (uint8_t)yy;
                    entry.month = (uint8_t)mm;
                    entry.day = (uint8_t)dd;
                    entry.has_date = true;
                    any = true;
                }
            }
            break;
        case TAG_ATC:
            if(flen >= 2) {
                entry.atc = (uint16_t)((v[0] << 8) | v[1]);
                entry.has_atc = true;
                any = true;
            }
            break;
        case TAG_LOG_MERCHANT:
            str_copy_trimmed(entry.merchant, sizeof(entry.merchant), v, flen);
            if(entry.merchant[0] != '\0') {
                entry.has_merchant = true;
                any = true;
            }
            break;
        default:
            break; /* cryptogram data, transaction type, unpredictable number */
        }

        rpos += flen;
    }

    /* Cards hand back their whole log area, unused slots included, and an
     * unused slot is all zeroes. Zeroed BCD is *valid* BCD, so an amount of
     * zero parses perfectly and would show up as a real 0.00 purchase. The
     * date is what separates a transaction from an empty slot: 00/00 is not a
     * date, so a record with no readable date and no merchant is discarded
     * rather than invented. Under-reporting the log is the safe direction. */
    if(!any) return false;
    if(!entry.has_date && !entry.has_merchant) return false;

    card->log[card->log_num] = entry;
    card->log_num++;
    return true;
}

/* ---------------------------------------------------------- presentation */

/* Amex prints 4-6-5; everyone else prints in fours. */
static bool group_break(EmvScheme scheme, size_t i, size_t pan_len) {
    if(scheme == EmvSchemeAmex && pan_len == 15) return i == 4 || i == 10;
    return i > 0 && (i % 4) == 0;
}

static void format_pan_impl(const EmvCard* card, char* out, size_t out_len, bool mask) {
    if(out == NULL || out_len == 0) return;
    out[0] = '\0';
    if(card == NULL || !card->has_pan || card->pan_len == 0) return;

    size_t pan_len = card->pan_len;
    size_t reveal_from = (pan_len > 4) ? pan_len - 4 : 0;
    size_t o = 0;

    for(size_t i = 0; i < pan_len; i++) {
        if(group_break(card->scheme, i, pan_len)) {
            if(o + 1 >= out_len) break;
            out[o++] = ' ';
        }
        if(o + 1 >= out_len) break;
        out[o++] = (mask && i < reveal_from) ? '*' : card->pan[i];
    }
    out[o] = '\0';
}

void emv_card_format_pan(const EmvCard* card, char* out, size_t out_len) {
    format_pan_impl(card, out, out_len, false);
}

void emv_card_format_pan_masked(const EmvCard* card, char* out, size_t out_len) {
    format_pan_impl(card, out, out_len, true);
}

bool emv_card_is_expired(const EmvCard* card, uint16_t now_year, uint8_t now_month) {
    if(card == NULL || !card->has_expiry) return false;
    if(now_month < 1 || now_month > 12) return false;

    /* Cards store two digits. Payment cards are issued for a handful of years,
     * never for eighty, so a two-digit year always means this century for any
     * card a person is carrying today. */
    uint16_t card_year = (uint16_t)(2000 + card->exp_year);

    /* The card is valid through the whole of its expiry month. */
    if(card_year != now_year) return card_year < now_year;
    return card->exp_month < now_month;
}

void emv_card_format_expiry(const EmvCard* card, char* out, size_t out_len) {
    if(out == NULL || out_len < 6) {
        if(out != NULL && out_len > 0) out[0] = '\0';
        return;
    }
    if(card == NULL || !card->has_expiry) {
        memcpy(out, "--/--", 6);
        return;
    }
    out[0] = (char)('0' + (card->exp_month / 10));
    out[1] = (char)('0' + (card->exp_month % 10));
    out[2] = '/';
    out[3] = (char)('0' + (card->exp_year / 10));
    out[4] = (char)('0' + (card->exp_year % 10));
    out[5] = '\0';
}
