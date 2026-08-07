#pragma once

#include <gui/canvas.h>

/* Truncate `text` in place until it fits `width` pixels, ending it with an
 * ellipsis when anything was cut.
 *
 * The firmware ships elements_string_fit_width, but it takes a FuriString, and
 * these views draw from fixed char buffers that live in the view model —
 * allocating a FuriString on every draw call to shorten a merchant name would
 * be a lot of heap traffic for a label.
 */
void text_fit_width(Canvas* canvas, char* text, uint16_t width);
