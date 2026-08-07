#include "emv_tlv.h"

#define EMV_TLV_MAX_DEPTH 8

void emv_tlv_iter_init(EmvTlvIter* iter, const uint8_t* buf, size_t len) {
    iter->buf = buf;
    iter->len = (buf == NULL) ? 0 : len;
    iter->pos = 0;
    iter->error = false;
}

/* Read a BER tag at `pos`. Returns false if it runs off the end.
 * Single byte unless the low five bits are all set, in which case subsequent
 * bytes continue while their top bit is set. EMV never goes past three bytes,
 * and we refuse to, so the tag always fits a uint32. */
static bool tlv_read_tag(const uint8_t* buf, size_t len, size_t* pos, EmvTag* tag, bool* constructed) {
    if(*pos >= len) return false;

    uint8_t first = buf[*pos];
    *constructed = (first & 0x20u) != 0;

    EmvTag t = first;
    (*pos)++;

    if((first & 0x1Fu) == 0x1Fu) {
        size_t extra = 0;
        for(;;) {
            if(*pos >= len) return false;
            uint8_t b = buf[*pos];
            (*pos)++;
            t = (t << 8) | b;
            extra++;
            if((b & 0x80u) == 0) break;
            if(extra >= 3) return false; /* longer than EMV allows */
        }
    }

    *tag = t;
    return true;
}

/* Read a BER length at `pos`. Short form is one byte <= 0x7F; long form has
 * 0x80 | n followed by n length bytes. We cap n at 3: a 16 MB EMV record does
 * not exist, and refusing here keeps the length inside size_t on any host. */
static bool tlv_read_len(const uint8_t* buf, size_t len, size_t* pos, size_t* out) {
    if(*pos >= len) return false;

    uint8_t first = buf[*pos];
    (*pos)++;

    if((first & 0x80u) == 0) {
        *out = first;
        return true;
    }

    size_t n = first & 0x7Fu;
    if(n == 0 || n > 3) return false; /* indefinite length is not legal in EMV */
    if(*pos + n > len) return false;

    size_t v = 0;
    for(size_t i = 0; i < n; i++) {
        v = (v << 8) | buf[*pos];
        (*pos)++;
    }
    *out = v;
    return true;
}

bool emv_tlv_iter_next(EmvTlvIter* iter, EmvTlv* out) {
    if(iter->buf == NULL) return false;

    /* 0x00 is inter-TLV padding and 0xFF is fill; both are skippable and both
     * appear in the wild between records. */
    while(iter->pos < iter->len && (iter->buf[iter->pos] == 0x00 || iter->buf[iter->pos] == 0xFF)) {
        iter->pos++;
    }
    if(iter->pos >= iter->len) return false;

    size_t pos = iter->pos;
    EmvTag tag = 0;
    bool constructed = false;
    size_t length = 0;

    if(!tlv_read_tag(iter->buf, iter->len, &pos, &tag, &constructed)) {
        iter->pos = iter->len; /* malformed: stop the walk rather than loop */
        iter->error = true;
        return false;
    }
    if(!tlv_read_len(iter->buf, iter->len, &pos, &length)) {
        iter->pos = iter->len;
        iter->error = true;
        return false;
    }
    if(length > iter->len - pos) {
        iter->pos = iter->len; /* truncated response */
        iter->error = true;
        return false;
    }

    out->tag = tag;
    out->constructed = constructed;
    out->value = iter->buf + pos;
    out->length = length;

    iter->pos = pos + length;
    return true;
}

/* Shared depth-first walk. `skip` counts matches still to be passed over
 * before one is returned; `counter`, when non-NULL, tallies every match
 * instead of stopping. */
static bool tlv_search(
    const uint8_t* buf,
    size_t len,
    EmvTag tag,
    size_t* skip,
    EmvTlv* out,
    size_t* counter,
    unsigned depth) {
    if(depth > EMV_TLV_MAX_DEPTH) return false;

    EmvTlvIter iter;
    emv_tlv_iter_init(&iter, buf, len);

    EmvTlv tlv;
    while(emv_tlv_iter_next(&iter, &tlv)) {
        if(tlv.tag == tag) {
            if(counter != NULL) {
                (*counter)++;
            } else if(*skip == 0) {
                *out = tlv;
                return true;
            } else {
                (*skip)--;
            }
        }
        if(tlv.constructed) {
            if(tlv_search(tlv.value, tlv.length, tag, skip, out, counter, depth + 1)) return true;
        }
    }
    return false;
}

bool emv_tlv_find(const uint8_t* buf, size_t len, EmvTag tag, EmvTlv* out) {
    return emv_tlv_find_nth(buf, len, tag, 0, out);
}

bool emv_tlv_find_nth(const uint8_t* buf, size_t len, EmvTag tag, size_t n, EmvTlv* out) {
    if(buf == NULL || out == NULL) return false;
    size_t skip = n;
    return tlv_search(buf, len, tag, &skip, out, NULL, 0);
}

size_t emv_tlv_count(const uint8_t* buf, size_t len, EmvTag tag) {
    if(buf == NULL) return 0;
    size_t skip = 0;
    size_t count = 0;
    EmvTlv scratch;
    tlv_search(buf, len, tag, &skip, &scratch, &count, 0);
    return count;
}

uint32_t emv_tlv_uint(const EmvTlv* tlv) {
    if(tlv == NULL || tlv->value == NULL) return 0;
    if(tlv->length == 0 || tlv->length > 4) return 0;

    uint32_t v = 0;
    for(size_t i = 0; i < tlv->length; i++) {
        v = (v << 8) | tlv->value[i];
    }
    return v;
}

bool emv_tlv_is_valid_sequence(const uint8_t* buf, size_t len) {
    if(buf == NULL || len == 0) return false;

    EmvTlvIter iter;
    emv_tlv_iter_init(&iter, buf, len);

    EmvTlv tlv;
    bool any = false;
    while(emv_tlv_iter_next(&iter, &tlv)) {
        any = true;
    }
    /* The iterator forces pos to len on failure too, so the error flag — not
     * the cursor — is what separates a clean walk from a bailout. */
    return any && !iter.error;
}
