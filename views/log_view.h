#pragma once

#include <gui/view.h>

#include "../helpers/emv_card.h"

typedef struct LogView LogView;

LogView* log_view_alloc(void);
void log_view_free(LogView* view);
View* log_view_get_view(LogView* view);

void log_view_set_card(LogView* view, const EmvCard* card);
