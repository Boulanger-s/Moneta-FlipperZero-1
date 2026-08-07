#include "../moneta_i.h"

/* One field, three questions: what it is, what it buys an attacker, and what
 * actually stops it. The third one is the reason this scene exists — a leak
 * with no answer is just a scare. */

void moneta_scene_explain_on_enter(void* context) {
    MonetaApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    FuriString* text = furi_string_alloc();

    if(app->explain_is_safe_fact) {
        LeakSafeFact fact = app->explain_safe;
        furi_string_printf(
            text,
            "\e#%s\e#\n"
            "Not readable over the air.\n\n"
            "%s\n",
            leak_safe_name(fact),
            leak_safe_text(fact));
    } else {
        LeakField field = app->explain_field;
        furi_string_printf(
            text,
            "\e#%s\e#\n"
            "Worth %u of 100 on the exposure score.\n\n"
            "\e#What it is\e#\n%s\n\n"
            "\e#What it enables\e#\n%s\n\n"
            "\e#What protects you\e#\n%s\n",
            leak_field_name(field),
            (unsigned)app->report.points[field],
            leak_field_what(field),
            leak_field_risk(field),
            leak_field_defence(field));
    }

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewWidget);
}

bool moneta_scene_explain_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_explain_on_exit(void* context) {
    MonetaApp* app = context;
    widget_reset(app->widget);
}
