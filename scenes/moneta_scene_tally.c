#include "../moneta_i.h"

/* What this Flipper has seen since the app opened.
 *
 * The single most useful screen at a talk: one card is an anecdote, nine cards
 * from nine different pockets is a finding. It counts only cards actually read
 * over the air — demo cards would make the number a fiction — and it is held
 * in RAM, so closing Moneta forgets all of it.
 */

void moneta_scene_tally_on_enter(void* context) {
    MonetaApp* app = context;
    Widget* widget = app->widget;
    const MonetaSession* s = &app->session;

    widget_reset(widget);

    FuriString* text = furi_string_alloc();
    furi_string_printf(text, "\e#This session\e#\n\n");

    if(s->cards == 0) {
        furi_string_cat_printf(
            text,
            "No cards read yet.\n\n"
            "Read a few and this screen turns one anecdote into a finding: how "
            "many cards in the room gave up a number, and how many gave up a "
            "name with it.\n\n"
            "Demo cards are not counted, and nothing here is written to disk.\n");
    } else {
        furi_string_cat_printf(text, "Cards read: %u\n", (unsigned)s->cards);
        furi_string_cat_printf(
            text, "Average exposure: %u%%\n\n", (unsigned)(s->score_total / s->cards));

        furi_string_cat_printf(text, "\e#Grades\e#\n");
        for(int g = LeakGradeF; g >= LeakGradeAPlus; g--) {
            uint8_t n = s->by_grade[g];
            if(n == 0) continue;
            furi_string_cat_printf(text, "%-3s %u  ", leak_grade_name((LeakGrade)g), (unsigned)n);
            for(uint8_t i = 0; i < n && i < 16; i++) furi_string_cat_printf(text, "#");
            furi_string_cat_printf(text, "\n");
        }

        furi_string_cat_printf(
            text,
            "\n\e#Of those\e#\n"
            "%u handed over a number and an expiry date - enough to try online.\n"
            "%u also gave a cardholder name.\n"
            "%u carried a readable spending log.\n",
            (unsigned)s->spendable,
            (unsigned)s->leaked_name,
            (unsigned)s->leaked_log);

        furi_string_cat_printf(
            text,
            "\nCounts real reads only. Nothing here is saved, and it is gone "
            "when you close the app.\n");
    }

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewWidget);
}

bool moneta_scene_tally_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_tally_on_exit(void* context) {
    MonetaApp* app = context;
    widget_reset(app->widget);
}
