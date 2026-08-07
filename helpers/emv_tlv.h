/* BER-TLV reader for EMV responses.
 *
 * Deliberately free of every Flipper header so the whole parser can be built
 * and fuzzed on a workstation (see test/). Everything an EMV card hands back —
 * the PPSE directory, the FCI, the GPO response, each record — is BER-TLV, so
 * this file is the floor the rest of Moneta stands on.
 *
 * The parser never copies and never allocates: a found value points straight
 * into the caller's response buffer, which must outlive the EmvTlv.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EMV tags are at most three bytes; we pack them big-endian into a uint32.
 * 0x57 stays 0x57, 0x5F24 stays 0x5F24, 0x9F4D stays 0x9F4D. */
typedef uint32_t EmvTag;

typedef struct {
    EmvTag tag;
    bool constructed; /* value is itself a TLV sequence */
    const uint8_t* value; /* borrowed — points into the source buffer */
    size_t length;
} EmvTlv;

typedef struct {
    const uint8_t* buf;
    size_t len;
    size_t pos;
    bool error; /* set when the walk stopped on a malformed or truncated TLV */
} EmvTlvIter;

/* Walk one TLV sequence, without descending. */
void emv_tlv_iter_init(EmvTlvIter* iter, const uint8_t* buf, size_t len);
bool emv_tlv_iter_next(EmvTlvIter* iter, EmvTlv* out);

/* Depth-first search for `tag` anywhere inside `buf`, descending into
 * constructed templates (6F, A5, BF0C, 61, 70, 77, 80...). Returns the first
 * match in document order. Recursion is bounded, so a hostile or corrupt
 * response cannot blow the stack. */
bool emv_tlv_find(const uint8_t* buf, size_t len, EmvTag tag, EmvTlv* out);

/* Same, but reports the n-th match (n = 0 is the first). Used for the PPSE
 * directory, where several `61` application templates sit side by side. */
bool emv_tlv_find_nth(const uint8_t* buf, size_t len, EmvTag tag, size_t n, EmvTlv* out);

/* Count matches of `tag` at any depth. */
size_t emv_tlv_count(const uint8_t* buf, size_t len, EmvTag tag);

/* Big-endian integer from a short value field (length 1..4). Returns 0 for
 * anything longer or empty. */
uint32_t emv_tlv_uint(const EmvTlv* tlv);

/* True when `buf` looks like a well-formed TLV sequence that consumes the whole
 * buffer. Used to decide whether a record is TLV or a raw log entry. */
bool emv_tlv_is_valid_sequence(const uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif
