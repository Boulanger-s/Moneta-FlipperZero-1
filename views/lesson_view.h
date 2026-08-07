#pragma once

#include <gui/view.h>

typedef struct LessonView LessonView;

LessonView* lesson_view_alloc(void);
void lesson_view_free(LessonView* view);
View* lesson_view_get_view(LessonView* view);

void lesson_view_reset(LessonView* view);
/* Drives the animation; call on a timer. */
void lesson_view_tick(LessonView* view);
