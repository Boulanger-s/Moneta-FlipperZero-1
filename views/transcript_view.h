#pragma once

#include <gui/view.h>

#include "../helpers/emv_reader.h"

typedef struct TranscriptView TranscriptView;

TranscriptView* transcript_view_alloc(void);
void transcript_view_free(TranscriptView* view);
View* transcript_view_get_view(TranscriptView* view);

void transcript_view_set(TranscriptView* view, const EmvTranscript* transcript);
