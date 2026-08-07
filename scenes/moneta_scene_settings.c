#include "../moneta_i.h"

static const char* const reveal_names[] = {"Hold OK", "Never"};
static const char* const on_off_names[] = {"Off", "On"};

static void reveal_changed(VariableItem* item) {
    MonetaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, reveal_names[index]);
    app->reveal = (MonetaRevealMode)index;
    card_view_set_reveal_allowed(app->card_view, app->reveal == MonetaRevealHold);
}

static void sound_changed(VariableItem* item) {
    MonetaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_names[index]);
    app->sound = index == 1;
}

static void vibro_changed(VariableItem* item) {
    MonetaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, on_off_names[index]);
    app->vibro = index == 1;
}

void moneta_scene_settings_on_enter(void* context) {
    MonetaApp* app = context;
    VariableItemList* list = app->var_item_list;

    variable_item_list_reset(list);

    VariableItem* item =
        variable_item_list_add(list, "Show number", 2, reveal_changed, app);
    variable_item_set_current_value_index(item, (uint8_t)app->reveal);
    variable_item_set_current_value_text(item, reveal_names[app->reveal]);

    item = variable_item_list_add(list, "Sound", 2, sound_changed, app);
    variable_item_set_current_value_index(item, app->sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off_names[app->sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibration", 2, vibro_changed, app);
    variable_item_set_current_value_index(item, app->vibro ? 1 : 0);
    variable_item_set_current_value_text(item, on_off_names[app->vibro ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, MonetaViewVarItemList);
}

bool moneta_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void moneta_scene_settings_on_exit(void* context) {
    MonetaApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
