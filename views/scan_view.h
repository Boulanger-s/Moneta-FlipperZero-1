#pragma once

#include <gui/view.h>

#include "../helpers/emv_reader.h"

typedef struct ScanView ScanView;

ScanView* scan_view_alloc(void);
void scan_view_free(ScanView* view);
View* scan_view_get_view(ScanView* view);

void scan_view_set_progress(ScanView* view, const EmvReaderProgress* progress);
/* Drives the idle animation; call on a timer. */
void scan_view_tick(ScanView* view);
