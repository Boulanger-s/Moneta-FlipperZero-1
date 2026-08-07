#pragma once

#include <gui/view.h>

#include "../helpers/emv_card.h"
#include "../helpers/leak_grade.h"

typedef struct CardView CardView;

CardView* card_view_alloc(void);
void card_view_free(CardView* view);
View* card_view_get_view(CardView* view);

void card_view_set_card(CardView* view, const EmvCard* card, const LeakReport* report);
/* When false, holding OK will not reveal the number — the setting wins. */
void card_view_set_reveal_allowed(CardView* view, bool allowed);
void card_view_tick(CardView* view);

/* Fired on a short OK press (open the field list) and on Right (open the
 * spending log, when the card had one). */
typedef void (*CardViewCallback)(void* context);
void card_view_set_fields_callback(CardView* view, CardViewCallback cb, void* context);
void card_view_set_log_callback(CardView* view, CardViewCallback cb, void* context);
