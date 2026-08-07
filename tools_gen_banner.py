#!/usr/bin/env python3
"""Render the Moneta GitHub banner and social-preview card.

The motif is the leak itself: a payment card with its number coming apart and
drifting off the edge into the air. Gold for money, red for what it costs.
Everything is drawn at 2x and downsampled, so the type stays crisp.

    python3 tools_gen_banner.py
"""

import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

BLACK_F = "/System/Library/Fonts/Supplemental/Arial Black.ttf"
BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
REG = "/System/Library/Fonts/Supplemental/Arial.ttf"
MONO = "/System/Library/Fonts/Supplemental/Andale Mono.ttf"

BG_TOP = (12, 13, 18)
BG_BOT = (22, 19, 28)
GOLD = (240, 180, 66)
GOLD_DIM = (150, 112, 40)
RED = (232, 63, 111)
WHITE = (242, 240, 236)
GRAY = (146, 142, 152)
DIM = (52, 48, 60)
CARD_FACE = (30, 30, 40)

SS = 2


def font(path, px):
    try:
        return ImageFont.truetype(path, px)
    except OSError:
        return ImageFont.truetype(BOLD, px)


def vgradient(w, h):
    img = Image.new("RGB", (w, h), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line(
            [(0, y), (w, y)],
            fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)),
        )
    return img


def glow(size, center, radius, color, strength=110):
    """A soft radial wash, for the light behind the card."""
    layer = Image.new("RGB", size, (0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = center
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=color)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.55))
    return Image.eval(layer, lambda v: int(v * strength / 255))


def draw_card(w, h):
    """The card itself, on its own transparent layer so it can be tilted."""
    card = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(card)

    r = int(h * 0.09)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=CARD_FACE, outline=GOLD_DIM, width=SS)

    # chip
    cw, ch = int(w * 0.13), int(h * 0.17)
    cx, cy = int(w * 0.09), int(h * 0.24)
    d.rounded_rectangle([cx, cy, cx + cw, cy + ch], radius=int(4 * SS), fill=GOLD_DIM)
    d.line([cx, cy + ch // 2, cx + cw, cy + ch // 2], fill=CARD_FACE, width=SS)
    d.line([cx + cw // 2, cy, cx + cw // 2, cy + ch], fill=CARD_FACE, width=SS)

    # contactless waves, top right
    wx, wy = int(w * 0.86), int(h * 0.36)
    for i, rad in enumerate((int(10 * SS), int(18 * SS), int(26 * SS))):
        d.arc(
            [wx - rad, wy - rad, wx + rad, wy + rad],
            -55,
            55,
            fill=GOLD if i == 0 else GOLD_DIM,
            width=SS + 1,
        )

    # the number: masked groups, then the four digits that are always visible
    f_num = font(MONO, int(h * 0.13))
    x = int(w * 0.08)
    y = int(h * 0.50)
    for group in ("****", "****", "****"):
        d.text((x, y), group, font=f_num, fill=GRAY)
        x += d.textlength(group, font=f_num) + int(h * 0.055)
    d.text((x, y), "0119", font=f_num, fill=WHITE)

    f_small = font(MONO, int(h * 0.085))
    d.text((int(w * 0.08), int(h * 0.76)), "EXP  09/28", font=f_small, fill=GRAY)

    return card


def escaping_digits(img, x0, y0):
    """Fragments of the number drifting off the card and out of frame."""
    d = ImageDraw.Draw(img, "RGBA")
    f = font(MONO, int(16 * SS))
    # Four fragments: a fifth is clipped by the frame and reads as a bug
    # rather than as data leaving.
    bits = ["4761", "7390", "0101", "0119"]
    x, y = x0, y0
    for i, bit in enumerate(bits):
        alpha = max(30, 210 - i * 40)
        d.text((x, y), bit, font=f, fill=GOLD + (alpha,))
        x += int(d.textlength(bit, font=f)) + int(8 * SS)
        y -= int(13 * SS)


def pill(d, x, y, text, f, fg, border):
    tw = d.textlength(text, font=f)
    pad_x, pad_y = int(11 * SS), int(6 * SS)
    h = int(f.size * 1.05) + pad_y * 2
    d.rounded_rectangle(
        [x, y, x + tw + pad_x * 2, y + h], radius=h // 2, outline=border, width=SS
    )
    d.text((x + pad_x, y + pad_y), text, font=f, fill=fg)
    return x + tw + pad_x * 2 + int(10 * SS)


def grade_stamp(d, x, y, size, letter="D"):
    d.rounded_rectangle([x, y, x + size, y + size], radius=int(size * 0.22), fill=RED)
    f = font(BLACK_F, int(size * 0.62))
    bbox = d.textbbox((0, 0), letter, font=f)
    d.text(
        (x + (size - (bbox[2] - bbox[0])) / 2 - bbox[0],
         y + (size - (bbox[3] - bbox[1])) / 2 - bbox[1]),
        letter,
        font=f,
        fill=(18, 10, 14),
    )


def compose(width, height, compact=False):
    W, Hh = width * SS, height * SS
    img = vgradient(W, Hh)

    # Light behind the card, added rather than blended so the background keeps
    # its own gradient underneath.
    img = ImageChops.add(
        img, glow((W, Hh), (int(W * 0.74), int(Hh * 0.46)), int(Hh * 0.55), GOLD, 90)
    )

    d = ImageDraw.Draw(img)

    # --- the card, tilted ---
    cw = int(W * 0.30)
    chh = int(cw / 1.6)  # a payment card is 85.6 x 54 mm
    card = draw_card(cw, chh)
    card = card.rotate(-7, resample=Image.BICUBIC, expand=True)
    rw, rh = card.size
    cx = int(W * 0.50) if not compact else int(W * 0.52)
    cy = int(Hh * 0.28) if not compact else int(Hh * 0.44)
    img.paste(card, (cx, cy), card)

    # The number coming apart off the card's right edge.
    escaping_digits(img, cx + int(rw * 0.97), cy + int(rh * 0.58))

    # --- wordmark ---
    left = int(W * 0.055)
    f_title = font(BLACK_F, int(Hh * (0.18 if not compact else 0.15)))
    f_tag = font(BOLD, int(Hh * (0.062 if not compact else 0.052)))
    f_sub = font(REG, int(Hh * (0.050 if not compact else 0.042)))
    f_pill = font(BOLD, int(Hh * 0.036))

    ty = int(Hh * (0.20 if not compact else 0.12))
    d.text((left, ty), "MONETA", font=f_title, fill=WHITE)

    ty += int(f_title.size * 1.28)
    d.text((left, ty), "She who warns.", font=f_tag, fill=GOLD)

    ty += int(f_tag.size * 1.6)
    d.text(
        (left, ty),
        "What a stranger's reader takes off your card,",
        font=f_sub,
        fill=GRAY,
    )
    ty += int(f_sub.size * 1.35)
    d.text((left, ty), "through your pocket, in about a second.", font=f_sub, fill=GRAY)

    # --- badges ---
    ty += int(f_sub.size * 2.1)
    x = left
    x = pill(d, x, ty, "READ-ONLY", f_pill, GOLD, GOLD_DIM)
    x = pill(d, x, ty, "EMV / ISO-DEP", f_pill, GRAY, DIM)
    x = pill(d, x, ty, "NO CVV. EVER.", f_pill, GRAY, DIM)

    # --- the grade, stamped on the card ---
    stamp = int(Hh * (0.150 if not compact else 0.125))
    grade_stamp(d, cx + rw - int(stamp * 1.05), cy + int(rh * 0.06), stamp, "D")

    return img.resize((width, height), Image.LANCZOS)


def main():
    banner = compose(1280, 400)
    p = os.path.join(OUT, "banner.png")
    banner.save(p)
    print("wrote", p)

    social = compose(1280, 640, compact=True)
    p = os.path.join(OUT, "social-preview.png")
    social.save(p)
    print("wrote", p)


if __name__ == "__main__":
    main()
