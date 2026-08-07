#include "../moneta_i.h"

void moneta_scene_about_on_enter(void* context) {
    MonetaApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    FuriString* text = furi_string_alloc();
    furi_string_printf(
        text,
        "\e#Moneta %s\e#\n"
        "Contactless payment leak educator.\n\n"
        "Moneta was the Roman epithet of Juno: \"she who warns\". Her temple "
        "minted Rome's coins, and gave us the word money. A warning about "
        "money seemed like the right name.\n\n"
        "\e#What it does\e#\n"
        "It has the same conversation with your card that a shop terminal "
        "does, and stops at the point where a terminal would take your money. "
        "Then it shows you everything the card said along the way.\n\n"
        "\e#What it never does\e#\n"
        "It does not write to the card, authorise anything, emulate anything, "
        "or make a payment. There is no transmit path for a transaction in "
        "this application. Nothing it reads is saved to the SD card, and "
        "nothing survives closing the app.\n\n"
        "\e#Use it on your own card\e#\n"
        "Reading a card belonging to someone who has not asked you to is "
        "wrong, and in most places illegal. The point of this tool is to show "
        "you what your own wallet gives away.\n\n"
        "\e#Author\e#\n"
        "at0m-b0mb\n"
        "github.com/at0m-b0mb/Moneta-FlipperZero\n\n"
        "MIT licensed.\n",
        MONETA_VERSION);

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewWidget);
}

bool moneta_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_about_on_exit(void* context) {
    MonetaApp* app = context;
    widget_reset(app->widget);
}
