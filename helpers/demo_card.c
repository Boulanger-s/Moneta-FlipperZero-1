#include "demo_card.h"
#include "demo_card_data.h"

#include <string.h>

size_t demo_card_count(void) {
    return MONETA_DEMO_CARD_NUM;
}

const char* demo_card_title(size_t index) {
    if(index >= MONETA_DEMO_CARD_NUM) return "";
    return demo_cards[index].title;
}

const char* demo_card_blurb(size_t index) {
    if(index >= MONETA_DEMO_CARD_NUM) return "";
    return demo_cards[index].blurb;
}

bool demo_card_load(size_t index, EmvCard* card) {
    if(card == NULL) return false;
    emv_card_reset(card);
    if(index >= MONETA_DEMO_CARD_NUM) return false;

    const DemoCardData* d = &demo_cards[index];

    /* Same order the reader uses on real hardware: directory, then the chosen
     * application, then the processing options, then the records. */
    emv_card_ingest_ppse(card, d->ppse, d->ppse_len);
    emv_card_ingest_tlv(card, d->fci, d->fci_len);
    emv_card_ingest_tlv(card, d->gpo, d->gpo_len);

    for(uint8_t i = 0; i < d->rec_num; i++) {
        emv_card_ingest_tlv(card, d->recs[i], d->rec_lens[i]);
        card->records_read++;
    }

    /* GET DATA answers arrive as a bare TLV, which ingest handles unchanged. */
    if(d->atc != NULL && d->atc_len >= 2) {
        card->atc = (uint16_t)((d->atc[0] << 8) | d->atc[1]);
        card->has_atc = true;
    }
    if(d->pin != NULL && d->pin_len >= 1) {
        card->pin_try = d->pin[0];
        card->has_pin_try = true;
    }

    if(d->log_fmt != NULL) {
        for(uint8_t i = 0; i < d->log_num; i++) {
            emv_card_ingest_log_record(
                card, d->log_fmt, d->log_fmt_len, d->logs[i], d->log_lens[i]);
        }
    }

    card->apdu_count = d->apdu_count;
    card->is_demo = true;
    return true;
}
