#include "emv_reader.h"

#include <furi.h>
#include <furi_hal_random.h>
#include <bit_buffer.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a_poller.h>

#include <string.h>

#define TAG "Moneta"

#define APDU_RESP_MAX 300u
#define APDU_CMD_MAX 64u
#define POLL_MS 20u

/* The card's own answers, in the order a terminal asks for them. */
#define SW_OK 0x9000u

struct EmvReader {
    Nfc* nfc;
    FuriThread* thread;
    FuriMutex* mutex;

    volatile bool stop;

    /* scanner -> worker */
    bool detected;
    size_t scan_num;
    NfcProtocol scan_stack[NfcProtocolNum];

    /* poller -> worker */
    NfcPoller* poller;
    volatile bool poll_done;
    bool poll_ok;

    /* published */
    EmvReaderProgress progress;
    EmvCard card;
    bool card_ready;

    /* scratch, owned by the poller callback */
    EmvCard building;
    EmvReaderProgress building_progress;
    uint8_t resp[APDU_RESP_MAX];
};

static void rd_lock(EmvReader* r) {
    furi_mutex_acquire(r->mutex, FuriWaitForever);
}
static void rd_unlock(EmvReader* r) {
    furi_mutex_release(r->mutex);
}

/* Publish progress from inside the poller callback. The GUI polls this. */
static void publish_progress(EmvReader* r, EmvReaderState state) {
    r->building_progress.state = state;
    rd_lock(r);
    r->progress = r->building_progress;
    rd_unlock(r);
}

/* ------------------------------------------------------------------ APDU */

/* One command-response pair over ISO-DEP.
 *
 * `iso14443_4a_poller_send_block` handles the block framing, so what goes in
 * is a bare APDU and what comes back is the response body with SW1SW2 on the
 * end. Two legacy status words still turn up on real cards and are handled
 * here rather than in every caller: 61xx ("more data waiting", fetch it with
 * GET RESPONSE) and 6Cxx ("wrong length, ask for exactly xx"). */
static bool apdu_exchange(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* cmd,
    size_t cmd_len,
    size_t* out_len,
    uint16_t* out_sw) {
    if(cmd_len == 0 || cmd_len > APDU_CMD_MAX) return false;

    uint8_t local[APDU_CMD_MAX];
    memcpy(local, cmd, cmd_len);

    *out_len = 0;
    *out_sw = 0;

    for(uint8_t attempt = 0; attempt < 3; attempt++) {
        bit_buffer_reset(tx);
        bit_buffer_reset(rx);
        bit_buffer_copy_bytes(tx, local, cmd_len);

        if(r->building_progress.apdus < 255) r->building_progress.apdus++;

        if(iso14443_4a_poller_send_block(poller, tx, rx) != Iso14443_4aErrorNone) return false;

        size_t n = bit_buffer_get_size_bytes(rx);
        if(n < 2) return false;

        uint8_t sw1 = bit_buffer_get_byte(rx, n - 2);
        uint8_t sw2 = bit_buffer_get_byte(rx, n - 1);
        size_t body = n - 2;

        if(sw1 == 0x61) {
            /* The body is empty and sw2 says how much is waiting. */
            local[0] = 0x00;
            local[1] = 0xC0; /* GET RESPONSE */
            local[2] = 0x00;
            local[3] = 0x00;
            local[4] = sw2;
            cmd_len = 5;
            continue;
        }
        if(sw1 == 0x6C) {
            /* Re-issue the same command with the length the card wants. */
            local[cmd_len - 1] = sw2;
            continue;
        }

        if(body > APDU_RESP_MAX) body = APDU_RESP_MAX;
        const uint8_t* data = bit_buffer_get_data(rx);
        memcpy(r->resp, data, body);

        *out_len = body;
        *out_sw = (uint16_t)((sw1 << 8) | sw2);
        return true;
    }
    return false;
}

/* SELECT by name (P1=04): the PPSE directory, or an application. */
static bool select_by_name(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* name,
    size_t name_len,
    size_t* out_len) {
    if(name_len == 0 || name_len > 16) return false;

    uint8_t cmd[5 + 16 + 1];
    cmd[0] = 0x00;
    cmd[1] = 0xA4;
    cmd[2] = 0x04;
    cmd[3] = 0x00;
    cmd[4] = (uint8_t)name_len;
    memcpy(cmd + 5, name, name_len);
    cmd[5 + name_len] = 0x00; /* Le */

    uint16_t sw = 0;
    if(!apdu_exchange(r, poller, tx, rx, cmd, name_len + 6, out_len, &sw)) return false;
    return sw == SW_OK && *out_len > 0;
}

/* What a terminal would put in each field the card asks for in its PDOL.
 *
 * None of this is sensitive and none of it is a lie the card can be harmed by
 * — a terminal supplies exactly these values on every tap. The unpredictable
 * number is genuinely random because the card expects it to be, and because a
 * fixed one would be the only part of this that resembled a replay. */
static void pdol_fill(EmvTag tag, uint8_t* out, size_t len) {
    memset(out, 0, len);

    switch(tag) {
    case 0x9F66: /* Terminal Transaction Qualifiers */
        if(len >= 1) out[0] = 0x36;
        break;
    case 0x9F02: /* Amount, Authorised — some cards decline a zero */
        if(len >= 6) out[5] = 0x01;
        break;
    case 0x9F1A: /* Terminal Country Code */
    case 0x5F2A: /* Transaction Currency Code */
        if(len >= 2) {
            out[0] = 0x08;
            out[1] = 0x40;
        }
        break;
    case 0x9A: /* Transaction Date, YYMMDD */
        if(len >= 3) {
            out[0] = 0x26;
            out[1] = 0x01;
            out[2] = 0x01;
        }
        break;
    case 0x9F37: /* Unpredictable Number */
        furi_hal_random_fill_buf(out, (uint32_t)len);
        break;
    case 0x9F35: /* Terminal Type: attended, online capable */
        if(len >= 1) out[0] = 0x22;
        break;
    case 0x9F33: /* Terminal Capabilities */
        if(len >= 3) {
            out[0] = 0xE0;
            out[1] = 0xF8;
            out[2] = 0xC8;
        }
        break;
    case 0x9F40: /* Additional Terminal Capabilities */
        if(len >= 1) out[0] = 0x60;
        break;
    case 0x9F09: /* Application Version Number */
        if(len >= 2) out[1] = 0x8C;
        break;
    default:
        break; /* zeros: TVR, CVM results, transaction type, everything else */
    }
}

/* GET PROCESSING OPTIONS. The card may demand a list of terminal values first
 * (its PDOL, tag 9F38); when it does not, the command carries an empty
 * template. */
static bool get_processing_options(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* pdol,
    size_t pdol_len,
    size_t* out_len) {
    uint8_t data[40];
    size_t data_len = 0;

    /* Walk the PDOL and lay each requested field down back to back, with no
     * tags — that is what tag 83 is: values only, in the order asked for. */
    size_t pos = 0;
    while(pos < pdol_len && data_len < sizeof(data)) {
        EmvTag tag = pdol[pos++];
        if((tag & 0x1Fu) == 0x1Fu) {
            uint8_t guard = 0;
            for(;;) {
                if(pos >= pdol_len) break;
                uint8_t b = pdol[pos++];
                tag = (tag << 8) | b;
                if((b & 0x80u) == 0) break;
                if(++guard >= 3) break;
            }
        }
        if(pos >= pdol_len) break;
        size_t flen = pdol[pos++];
        if(flen == 0) continue;
        if(data_len + flen > sizeof(data)) break; /* PDOL longer than we serve */

        pdol_fill(tag, data + data_len, flen);
        data_len += flen;
    }

    uint8_t cmd[6 + sizeof(data) + 3];
    cmd[0] = 0x80;
    cmd[1] = 0xA8;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = (uint8_t)(data_len + 2);
    cmd[5] = 0x83; /* Command Template */
    cmd[6] = (uint8_t)data_len;
    memcpy(cmd + 7, data, data_len);
    cmd[7 + data_len] = 0x00; /* Le */

    uint16_t sw = 0;
    if(!apdu_exchange(r, poller, tx, rx, cmd, data_len + 8, out_len, &sw)) return false;
    return sw == SW_OK && *out_len > 0;
}

static bool read_record(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    uint8_t sfi,
    uint8_t record,
    size_t* out_len) {
    uint8_t cmd[5];
    cmd[0] = 0x00;
    cmd[1] = 0xB2;
    cmd[2] = record;
    cmd[3] = (uint8_t)((sfi << 3) | 0x04u); /* P2: read record `record` in `sfi` */
    cmd[4] = 0x00;

    uint16_t sw = 0;
    if(!apdu_exchange(r, poller, tx, rx, cmd, sizeof(cmd), out_len, &sw)) return false;
    return sw == SW_OK && *out_len > 0;
}

static bool get_data(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    uint16_t tag,
    size_t* out_len) {
    uint8_t cmd[5];
    cmd[0] = 0x80;
    cmd[1] = 0xCA;
    cmd[2] = (uint8_t)(tag >> 8);
    cmd[3] = (uint8_t)(tag & 0xFF);
    cmd[4] = 0x00;

    uint16_t sw = 0;
    if(!apdu_exchange(r, poller, tx, rx, cmd, sizeof(cmd), out_len, &sw)) return false;
    return sw == SW_OK && *out_len > 0;
}

/* ------------------------------------------------------- the conversation */

static const uint8_t PPSE_NAME[] = "2PAY.SYS.DDF01";

/* Cards that do not publish a directory still answer a direct SELECT. These
 * are the applications common enough to be worth one command each. */
static const struct {
    uint8_t aid[7];
    uint8_t len;
} FALLBACK_AIDS[] = {
    {{0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10}, 7}, /* Visa */
    {{0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10}, 7}, /* Mastercard */
    {{0xA0, 0x00, 0x00, 0x00, 0x25, 0x01, 0x00}, 6}, /* Amex */
    {{0xA0, 0x00, 0x00, 0x03, 0x33, 0x01, 0x01}, 7}, /* UnionPay */
    {{0xA0, 0x00, 0x00, 0x00, 0x65, 0x10, 0x10}, 7}, /* JCB */
    {{0xA0, 0x00, 0x00, 0x05, 0x24, 0x10, 0x10}, 7}, /* RuPay */
};

/* Read the transaction log, when the card admits to keeping one. Tag 9F4D
 * names the file and how many entries it holds; tag 9F4F describes the layout
 * of each entry. Without both, the bytes are unreadable and we do not guess. */
static void read_transaction_log(
    EmvReader* r,
    Iso14443_4aPoller* poller,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* fci,
    size_t fci_len) {
    uint8_t log_sfi = 0;
    uint8_t log_count = 0;

    EmvTlv tlv;
    if(emv_tlv_find(fci, fci_len, 0x9F4D, &tlv) && tlv.length >= 2) {
        log_sfi = tlv.value[0];
        log_count = tlv.value[1];
    } else {
        size_t n = 0;
        if(get_data(r, poller, tx, rx, 0x9F4D, &n) && n >= 2) {
            /* GET DATA answers with the tag wrapped around the value. */
            if(emv_tlv_find(r->resp, n, 0x9F4D, &tlv) && tlv.length >= 2) {
                log_sfi = tlv.value[0];
                log_count = tlv.value[1];
            } else if(n >= 2) {
                log_sfi = r->resp[0];
                log_count = r->resp[1];
            }
        }
    }

    if(log_sfi == 0 || log_count == 0) return;

    uint8_t fmt[64];
    size_t fmt_len = 0;
    size_t n = 0;
    if(get_data(r, poller, tx, rx, 0x9F4F, &n) && n > 0) {
        if(emv_tlv_find(r->resp, n, 0x9F4F, &tlv) && tlv.length > 0 && tlv.length <= sizeof(fmt)) {
            memcpy(fmt, tlv.value, tlv.length);
            fmt_len = tlv.length;
        }
    }
    if(fmt_len == 0) return; /* no layout, no honest reading */

    publish_progress(r, EmvReaderReadingLog);

    if(log_count > MONETA_LOG_MAX) log_count = MONETA_LOG_MAX;
    for(uint8_t rec = 1; rec <= log_count && !r->stop; rec++) {
        size_t len = 0;
        if(!read_record(r, poller, tx, rx, log_sfi, rec, &len)) continue;
        if(emv_card_ingest_log_record(&r->building, fmt, fmt_len, r->resp, len)) {
            r->building_progress.log_entries = r->building.log_num;
            publish_progress(r, EmvReaderReadingLog);
        }
    }
}

/* The whole read, start to finish. Runs inside the poller callback, which is
 * the only context where talking to the card is legal. */
static bool run_emv_session(EmvReader* r, Iso14443_4aPoller* poller) {
    BitBuffer* tx = bit_buffer_alloc(APDU_CMD_MAX);
    BitBuffer* rx = bit_buffer_alloc(APDU_RESP_MAX);
    bool got_anything = false;

    emv_card_reset(&r->building);
    memset(&r->building_progress, 0, sizeof(r->building_progress));

    uint8_t aid[MONETA_AID_MAX];
    size_t aid_len = 0;

    do {
        /* 1. Ask the card what it can pay with. */
        publish_progress(r, EmvReaderSelecting);
        size_t len = 0;
        if(select_by_name(r, poller, tx, rx, PPSE_NAME, sizeof(PPSE_NAME) - 1, &len)) {
            emv_card_ingest_ppse(&r->building, r->resp, len);
        }

        if(r->building.app_num > 0) {
            aid_len = r->building.apps[0].aid_len;
            memcpy(aid, r->building.apps[0].aid, aid_len);
        }

        /* 2. No directory? Knock on the doors we know the names of. */
        if(aid_len == 0) {
            for(size_t i = 0; i < sizeof(FALLBACK_AIDS) / sizeof(FALLBACK_AIDS[0]); i++) {
                if(r->stop) break;
                if(select_by_name(
                       r, poller, tx, rx, FALLBACK_AIDS[i].aid, FALLBACK_AIDS[i].len, &len)) {
                    aid_len = FALLBACK_AIDS[i].len;
                    memcpy(aid, FALLBACK_AIDS[i].aid, aid_len);
                    break;
                }
            }
            if(aid_len == 0) break; /* answered ISO-DEP, but pays for nothing */
        }

        /* 3. Open the application. Its FCI carries the label, and the list of
         *    values the card wants from the terminal before it will talk. */
        publish_progress(r, EmvReaderOpeningApp);
        if(!select_by_name(r, poller, tx, rx, aid, aid_len, &len)) break;

        uint8_t fci[APDU_RESP_MAX];
        size_t fci_len = len;
        memcpy(fci, r->resp, fci_len);
        emv_card_ingest_tlv(&r->building, fci, fci_len);
        got_anything = true;

        uint8_t pdol[64];
        size_t pdol_len = 0;
        EmvTlv tlv;
        if(emv_tlv_find(fci, fci_len, 0x9F38, &tlv) && tlv.length <= sizeof(pdol)) {
            memcpy(pdol, tlv.value, tlv.length);
            pdol_len = tlv.length;
        }

        /* 4. Processing options. The answer contains the Application File
         *    Locator: which files hold the card's data, and which records. */
        if(!get_processing_options(r, poller, tx, rx, pdol, pdol_len, &len)) break;

        uint8_t afl[64];
        size_t afl_len = 0;
        emv_card_ingest_tlv(&r->building, r->resp, len);

        if(emv_tlv_find(r->resp, len, 0x94, &tlv)) {
            /* Format 2: the AFL is tagged inside a 77 template. */
            if(tlv.length <= sizeof(afl)) {
                memcpy(afl, tlv.value, tlv.length);
                afl_len = tlv.length;
            }
        } else if(emv_tlv_find(r->resp, len, 0x80, &tlv) && tlv.length > 2) {
            /* Format 1: a bare 80 template, two bytes of AIP then the AFL. */
            afl_len = tlv.length - 2;
            if(afl_len > sizeof(afl)) afl_len = sizeof(afl);
            memcpy(afl, tlv.value + 2, afl_len);
        }

        /* 5. Read what the locator points at. This is the step that produces
         *    the card number. */
        publish_progress(r, EmvReaderReadingRecords);
        for(size_t i = 0; i + 3 < afl_len; i += 4) {
            if(r->stop) break;
            uint8_t sfi = afl[i] >> 3;
            uint8_t first = afl[i + 1];
            uint8_t last = afl[i + 2];
            if(sfi == 0 || sfi == 31 || first == 0 || last < first) continue;
            if(last - first > 16) last = first + 16; /* a sane bound on a hostile AFL */

            for(uint8_t rec = first; rec <= last; rec++) {
                if(r->stop) break;
                size_t rlen = 0;
                if(!read_record(r, poller, tx, rx, sfi, rec, &rlen)) continue;
                emv_card_ingest_tlv(&r->building, r->resp, rlen);
                r->building.records_read++;
                r->building_progress.records = r->building.records_read;
                publish_progress(r, EmvReaderReadingRecords);
            }
        }

        /* 6. The extras a terminal never needs but a reader can still have. */
        if(!r->stop && get_data(r, poller, tx, rx, 0x9F36, &len) && len >= 2) {
            if(emv_tlv_find(r->resp, len, 0x9F36, &tlv) && tlv.length >= 2) {
                r->building.atc = (uint16_t)((tlv.value[0] << 8) | tlv.value[1]);
                r->building.has_atc = true;
            }
        }
        if(!r->stop && get_data(r, poller, tx, rx, 0x9F17, &len) && len >= 1) {
            if(emv_tlv_find(r->resp, len, 0x9F17, &tlv) && tlv.length >= 1) {
                r->building.pin_try = tlv.value[0];
                r->building.has_pin_try = true;
            }
        }

        if(!r->stop) read_transaction_log(r, poller, tx, rx, fci, fci_len);
    } while(0);

    bit_buffer_free(tx);
    bit_buffer_free(rx);

    r->building.apdu_count = r->building_progress.apdus;
    return got_anything;
}

/* -------------------------------------------------------------- callbacks */

static void scanner_cb(NfcScannerEvent event, void* context) {
    EmvReader* r = context;
    if(event.type != NfcScannerEventTypeDetected) return;

    rd_lock(r);
    r->scan_num = event.data.protocol_num;
    if(r->scan_num > NfcProtocolNum) r->scan_num = NfcProtocolNum;
    for(size_t i = 0; i < r->scan_num; i++) {
        r->scan_stack[i] = event.data.protocols[i];
    }
    r->detected = true;
    rd_unlock(r);
}

static NfcCommand poller_cb(NfcGenericEvent event, void* context) {
    EmvReader* r = context;
    const Iso14443_4aPollerEvent* ev = event.event_data;

    if(ev->type == Iso14443_4aPollerEventTypeReady) {
        bool ok = run_emv_session(r, (Iso14443_4aPoller*)event.instance);

        rd_lock(r);
        r->poll_ok = ok;
        if(ok) {
            r->card = r->building;
            r->card_ready = true;
        }
        rd_unlock(r);

        r->poll_done = true;
        return NfcCommandStop;
    }

    if(ev->type == Iso14443_4aPollerEventTypeError) {
        r->poll_ok = false;
        r->poll_done = true;
        return NfcCommandStop;
    }
    return NfcCommandContinue;
}

/* ---------------------------------------------------------------- worker */

static bool wait_flag(EmvReader* r, const volatile bool* flag) {
    while(!r->stop) {
        if(*flag) return true;
        furi_delay_ms(POLL_MS);
    }
    return false;
}

/* What to call a card that turned out not to be a payment card. Only the
 * families a Flipper can actually land on are named; anything else is
 * reported honestly as unknown rather than guessed at. */
static const char* protocol_label(NfcProtocol p) {
    switch(p) {
    case NfcProtocolMfClassic:
        return "Mifare Classic";
    case NfcProtocolMfUltralight:
        return "Ultralight/NTAG";
    case NfcProtocolMfDesfire:
        return "DESFire";
    case NfcProtocolIso15693_3:
        return "ISO15693 tag";
    case NfcProtocolFelica:
        return "FeliCa";
    case NfcProtocolSlix:
        return "SLIX tag";
    case NfcProtocolSt25tb:
        return "ST25TB tag";
    case NfcProtocolIso14443_3a:
        return "Basic NFC-A tag";
    case NfcProtocolIso14443_3b:
        return "Basic NFC-B tag";
    case NfcProtocolIso14443_4a:
        return "Smartcard";
    case NfcProtocolIso14443_4b:
        return "NFC-B smartcard";
    default:
        return "Unknown card";
    }
}

static bool stack_has_iso_dep(const NfcProtocol* stack, size_t num) {
    for(size_t i = 0; i < num; i++) {
        if(stack[i] == NfcProtocolIso14443_4a) return true;
    }
    return false;
}

/* The deepest protocol in the stack is the one worth naming — "Mifare
 * Classic", not the ISO14443-3A layer it happens to sit on. */
static NfcProtocol stack_top(const NfcProtocol* stack, size_t num) {
    NfcProtocol top = stack[0];
    for(size_t i = 1; i < num; i++) {
        if(nfc_protocol_has_parent(stack[i], top)) top = stack[i];
    }
    return top;
}

static int32_t reader_worker(void* context) {
    EmvReader* r = context;

    r->nfc = nfc_alloc();

    while(!r->stop) {
        /* --- sweep for anything in the field --- */
        rd_lock(r);
        r->detected = false;
        r->progress.state = EmvReaderSearching;
        r->progress.other_card[0] = '\0';
        rd_unlock(r);

        NfcScanner* scanner = nfc_scanner_alloc(r->nfc);
        nfc_scanner_start(scanner, scanner_cb, r);
        bool found = wait_flag(r, (const volatile bool*)&r->detected);
        nfc_scanner_stop(scanner);
        nfc_scanner_free(scanner);
        if(!found) break;

        rd_lock(r);
        size_t num = r->scan_num;
        NfcProtocol stack[NfcProtocolNum];
        for(size_t i = 0; i < num; i++) stack[i] = r->scan_stack[i];
        rd_unlock(r);

        if(num == 0) continue;

        /* --- a payment card speaks ISO-DEP; nothing else does --- */
        if(!stack_has_iso_dep(stack, num)) {
            const char* label = protocol_label(stack_top(stack, num));
            rd_lock(r);
            strncpy(r->progress.other_card, label, sizeof(r->progress.other_card) - 1);
            r->progress.other_card[sizeof(r->progress.other_card) - 1] = '\0';
            r->progress.state = EmvReaderNotPayment;
            rd_unlock(r);
            /* Hold the verdict on screen rather than instantly re-scanning the
             * same card and flickering between two messages. */
            for(int i = 0; i < 60 && !r->stop; i++) furi_delay_ms(POLL_MS);
            continue;
        }

        /* --- run the EMV conversation --- */
        r->poll_done = false;
        r->poll_ok = false;

        r->poller = nfc_poller_alloc(r->nfc, NfcProtocolIso14443_4a);
        nfc_poller_start(r->poller, poller_cb, r);
        wait_flag(r, &r->poll_done);
        nfc_poller_stop(r->poller);
        nfc_poller_free(r->poller);
        r->poller = NULL;

        rd_lock(r);
        bool done = r->card_ready;
        r->progress.state = done ? EmvReaderDone : EmvReaderLost;
        rd_unlock(r);

        if(done) break;
        /* The card moved away mid-read. Say so, then go back to looking. */
        for(int i = 0; i < 30 && !r->stop; i++) furi_delay_ms(POLL_MS);
    }

    nfc_free(r->nfc);
    r->nfc = NULL;
    return 0;
}

/* ------------------------------------------------------------- lifecycle */

EmvReader* emv_reader_alloc(void) {
    EmvReader* r = malloc(sizeof(EmvReader));
    memset(r, 0, sizeof(EmvReader));
    r->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    r->progress.state = EmvReaderIdle;
    return r;
}

void emv_reader_free(EmvReader* r) {
    furi_assert(r);
    emv_reader_stop(r);
    furi_mutex_free(r->mutex);
    free(r);
}

void emv_reader_start(EmvReader* r) {
    furi_assert(r);
    emv_reader_stop(r);

    r->stop = false;
    rd_lock(r);
    memset(&r->progress, 0, sizeof(r->progress));
    r->progress.state = EmvReaderSearching;
    r->card_ready = false;
    emv_card_reset(&r->card);
    rd_unlock(r);

    r->thread = furi_thread_alloc_ex("MonetaReader", 4 * 1024, reader_worker, r);
    furi_thread_start(r->thread);
}

void emv_reader_stop(EmvReader* r) {
    furi_assert(r);
    r->stop = true;
    if(r->thread) {
        furi_thread_join(r->thread);
        furi_thread_free(r->thread);
        r->thread = NULL;
    }
}

void emv_reader_progress(EmvReader* r, EmvReaderProgress* out) {
    furi_assert(r);
    furi_assert(out);
    rd_lock(r);
    *out = r->progress;
    rd_unlock(r);
}

bool emv_reader_get_card(EmvReader* r, EmvCard* out) {
    furi_assert(r);
    furi_assert(out);
    rd_lock(r);
    bool ready = r->card_ready;
    if(ready) *out = r->card;
    rd_unlock(r);
    return ready;
}
