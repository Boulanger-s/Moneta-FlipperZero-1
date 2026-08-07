/* The radio half: everything that actually talks to a card.
 *
 * Moneta only ever reads. There is no command in this file that changes a
 * single byte on the card, authorises anything, or produces a payment. It is
 * the same conversation a shop terminal starts — SELECT the payment directory,
 * SELECT an application, ask for its processing options, read the records it
 * points at — stopped at the point where a terminal would ask for a cryptogram
 * and take your money.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "emv_card.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EmvReaderIdle = 0,
    EmvReaderSearching, /* field is up, nothing in it yet */
    EmvReaderSelecting, /* SELECT PPSE — asking what the card can pay with */
    EmvReaderOpeningApp, /* SELECT AID + GET PROCESSING OPTIONS */
    EmvReaderReadingRecords, /* READ RECORD across the file locator */
    EmvReaderReadingLog, /* the transaction log, if the card keeps one */
    EmvReaderDone,
    EmvReaderNotPayment, /* a card, but not one that speaks ISO-DEP */
    EmvReaderNoPayApp, /* an ISO-DEP smartcard with no payment application */
    EmvReaderLost, /* moved away mid-read */
} EmvReaderState;

/* --------------------------------------------------------------- transcript
 *
 * Every command and answer, kept so the user can see the actual conversation
 * rather than take our word for it. It is the most convincing screen in the
 * application for anyone technical, and the only way to diagnose a card that
 * behaves unusually without a debugger.
 */

#define MONETA_TRX_MAX 26
#define MONETA_TRX_CMD_MAX 16
#define MONETA_TRX_RESP_MAX 24
#define MONETA_TRX_LABEL_MAX 15

typedef struct {
    char label[MONETA_TRX_LABEL_MAX + 1]; /* "SELECT PPSE", "READ 1/2" */
    uint8_t cmd[MONETA_TRX_CMD_MAX];
    uint8_t cmd_stored; /* bytes kept */
    uint8_t cmd_len; /* bytes actually sent */
    uint8_t resp[MONETA_TRX_RESP_MAX];
    uint8_t resp_stored;
    uint16_t resp_len; /* bytes actually received, SW excluded */
    uint16_t sw; /* status word, 0 when the exchange failed outright */
    bool failed;
} EmvApduEntry;

typedef struct {
    EmvApduEntry entry[MONETA_TRX_MAX];
    uint8_t num;
    bool truncated; /* the conversation outran the buffer */
} EmvTranscript;

typedef struct {
    EmvReaderState state;
    uint8_t records; /* records read so far */
    uint8_t apdus; /* commands sent — "how much work a skimmer does" */
    uint8_t log_entries;
    char other_card[24]; /* what it was, when it was not a payment card */
} EmvReaderProgress;

typedef struct EmvReader EmvReader;

EmvReader* emv_reader_alloc(void);
void emv_reader_free(EmvReader* reader);

/* Bring the field up and keep trying until a card is read or stop is called. */
void emv_reader_start(EmvReader* reader);
/* Blocks until the worker has stopped and the radio is released. */
void emv_reader_stop(EmvReader* reader);

void emv_reader_progress(EmvReader* reader, EmvReaderProgress* out);
/* True once a card has been read; fills `out` with what came off it. */
bool emv_reader_get_card(EmvReader* reader, EmvCard* out);
/* The command log for the most recent read. */
void emv_reader_get_transcript(EmvReader* reader, EmvTranscript* out);

#ifdef __cplusplus
}
#endif
