#include "text_fit.h"

#include <string.h>

#define FIT_BUF_MAX 64

void text_fit_width(Canvas* canvas, char* text, uint16_t width) {
    if(text == NULL || text[0] == '\0') return;
    if(canvas_string_width(canvas, text) <= width) return;

    size_t len = strlen(text);
    if(len + 3 > FIT_BUF_MAX) len = FIT_BUF_MAX - 3;

    char buf[FIT_BUF_MAX];
    while(len > 0) {
        len--;
        memcpy(buf, text, len);
        buf[len] = '.';
        buf[len + 1] = '.';
        buf[len + 2] = '\0';
        if(canvas_string_width(canvas, buf) <= width) {
            memcpy(text, buf, len + 3);
            return;
        }
    }

    /* Not even ".." fits. Better an empty label than a broken one. */
    text[0] = '\0';
}
