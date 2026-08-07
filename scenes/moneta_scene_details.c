#include "../moneta_i.h"

/* The technical read-out: what the card said about itself, beyond the fields
 * that make the point. Useful for anyone who wants to check the grade against
 * the evidence, and for telling two similar-looking cards apart. */

static void append_hex(FuriString* s, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        furi_string_cat_printf(s, "%02X", data[i]);
    }
}

void moneta_scene_details_on_enter(void* context) {
    MonetaApp* app = context;
    Widget* widget = app->widget;
    const EmvCard* c = &app->card;

    widget_reset(widget);

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "\e#Card details\e#\n");

    furi_string_cat_printf(text, "Scheme: %s\n", emv_scheme_name(c->scheme));
    if(c->has_label) furi_string_cat_printf(text, "Label: %s\n", c->label);

    /* Expiry, with a verdict rather than just the digits. */
    char exp[16];
    emv_card_format_expiry(c, exp, sizeof(exp));
    furi_string_cat_printf(text, "Expiry: %s", exp);
    if(c->has_expiry) {
        furi_string_cat_printf(text, moneta_card_expired(app) ? " (EXPIRED)\n" : " (valid)\n");
    } else {
        furi_string_cat_printf(text, "\n");
    }

    if(c->has_pan) {
        furi_string_cat_printf(
            text,
            "Number: %u digits, %s\n  read from tag %s\n",
            (unsigned)c->pan_len,
            c->pan_luhn_ok ? "check digit valid" : "check digit INVALID",
            c->pan_from_track2 ? "57 (Track 2)" : "5A");
    }

    if(c->aid_len > 0) {
        furi_string_cat_printf(text, "AID: ");
        append_hex(text, c->aid, c->aid_len);
        furi_string_cat_printf(text, "\n");
    }

    /* Several applications on one card is common — a domestic scheme sitting
     * beside an international one. Moneta reads the highest-priority one and
     * says so rather than pretending the others are not there. */
    if(c->app_num > 1) {
        furi_string_cat_printf(
            text, "\n\e#%u applications\e#\nMoneta read the first.\n", (unsigned)c->app_num);
        for(uint8_t i = 0; i < c->app_num; i++) {
            furi_string_cat_printf(text, "%u. ", (unsigned)(i + 1));
            append_hex(text, c->apps[i].aid, c->apps[i].aid_len);
            if(c->apps[i].label[0]) furi_string_cat_printf(text, " %s", c->apps[i].label);
            furi_string_cat_printf(text, "\n");
        }
    }

    furi_string_cat_printf(text, "\n\e#Chip data\e#\n");

    if(c->has_country) {
        const char* country = emv_country_name(c->country);
        if(country) {
            furi_string_cat_printf(text, "Issuer country: %s\n", country);
        } else {
            furi_string_cat_printf(text, "Issuer country: %u\n", (unsigned)c->country);
        }
    }
    if(c->has_currency) {
        const char* cur = emv_currency_name(c->currency);
        if(cur) {
            furi_string_cat_printf(text, "Currency: %s\n", cur);
        } else {
            furi_string_cat_printf(text, "Currency: %u\n", (unsigned)c->currency);
        }
    }
    if(c->has_service_code) {
        furi_string_cat_printf(
            text,
            "Service code: %u%u%u\n",
            (unsigned)c->service_code[0],
            (unsigned)c->service_code[1],
            (unsigned)c->service_code[2]);
    }
    if(c->has_atc) {
        furi_string_cat_printf(
            text, "Taps so far (ATC): %u\n", (unsigned)c->atc);
    }
    if(c->has_pin_try) {
        furi_string_cat_printf(text, "PIN tries left: %u\n", (unsigned)c->pin_try);
    }
    if(c->name_placeholder) {
        furi_string_cat_printf(text, "Cardholder: withheld by issuer\n");
    }

    furi_string_cat_printf(
        text,
        "\n\e#The read\e#\nCommands sent: %u\nRecords read: %u\nLog entries: %u\n",
        (unsigned)c->apdu_count,
        (unsigned)c->records_read,
        (unsigned)c->log_num);

    if(c->is_demo) {
        furi_string_cat_printf(
            text, "\nThis is a built-in demo card, not one that was read over the air.\n");
    }

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewWidget);
}

bool moneta_scene_details_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_details_on_exit(void* context) {
    MonetaApp* app = context;
    widget_reset(app->widget);
}
