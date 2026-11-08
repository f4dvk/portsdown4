#!/usr/bin/env python3
# make_sonde_icon.py : icone de radiosonde (chaine de sondage vue de profil :
# ballon latex, parachute, suspente, sonde et son antenne), dessinee en
# vectoriel (pycairo) puis rendue pour :
#   - la carte web : web/sonde.png (2 pixels par unite, affichee a 1/2) ;
#   - l'ecran du Pi : md_sonde_icon.c (1 pixel par unite).
#
#   python3 make_sonde_icon.py        (dans src/multidec, sur un PC)
#
# Les fichiers generes sont gardes dans le depot : le Pi n'a besoin que du
# compilateur C. Le point d'ancrage (la position de la sonde) est le centre de
# la boite de la sonde : (W / 2, BOX_CY).

import math
import os
import cairo
from PIL import Image

W, H = 22, 48                     # unites (= pixels CSS sur la carte web)
BOX_CY = 43.1                     # centre de la boite de la sonde
WEB_SCALE, SCREEN_SCALE = 2, 1


def draw(ctx):
    cx = W / 2
    # ballon latex : sphere un peu etiree, col en bas
    r, by = 9, 9.5
    a = math.radians(25)
    ctx.new_path()
    ctx.arc(cx, by, r, math.pi - a, 2 * math.pi + a)
    ctx.curve_to(cx + 7, 16.5, cx + 2, 19, cx + 0.6, 20.3)
    ctx.line_to(cx - 0.6, 20.3)
    ctx.curve_to(cx - 2, 19, cx - 7, 16.5, cx - r * math.cos(a), by + r * math.sin(a))
    ctx.close_path()
    g = cairo.RadialGradient(cx - 3, 6, 0.4, cx, 10, 11.5)
    g.add_color_stop_rgb(0, 1, 1, 1)
    g.add_color_stop_rgb(0.5, 0.93, 0.94, 0.95)
    g.add_color_stop_rgb(1, 0.66, 0.69, 0.74)
    ctx.set_source(g)
    ctx.fill_preserve()
    ctx.set_source_rgba(0.35, 0.38, 0.43, 0.9)
    ctx.set_line_width(0.7)
    ctx.stroke()
    # noeud du col
    ctx.rectangle(cx - 0.8, 20.1, 1.6, 1.3)
    ctx.set_source_rgb(0.45, 0.47, 0.5)
    ctx.fill()
    # ficelle jusqu'au parachute
    line(ctx, cx, 21.4, cx, 24, 0.6)
    # parachute : dome orange, fuseaux, suspentes
    ctx.save()
    ctx.translate(cx, 27.5)
    ctx.scale(5, 3.8)
    ctx.new_path()
    ctx.arc(0, 0, 1, math.pi, 2 * math.pi)
    ctx.restore()
    ctx.curve_to(cx + 3.5, 26.9, cx - 3.5, 26.9, cx - 5, 27.5)
    ctx.close_path()
    g = cairo.LinearGradient(0, 23.7, 0, 27.6)
    g.add_color_stop_rgb(0, 1, 0.6, 0.3)
    g.add_color_stop_rgb(1, 0.88, 0.27, 0.05)
    ctx.set_source(g)
    ctx.fill_preserve()
    ctx.set_source_rgb(0.5, 0.15, 0.03)
    ctx.set_line_width(0.5)
    ctx.stroke()
    for x in (-2.5, 0, 2.5):
        ctx.move_to(cx, 23.9)
        ctx.curve_to(cx + x * 0.6, 24.5, cx + x, 26, cx + x, 27.2)
    ctx.set_source_rgba(0.6, 0.18, 0.03, 0.8)
    ctx.set_line_width(0.35)
    ctx.stroke()
    for x in (-4.8, -2.5, 2.5, 4.8):
        line(ctx, cx + x, 27.5, cx, 31.5, 0.35)
    # suspente jusqu'a la sonde
    line(ctx, cx, 31.5, cx, 40, 0.6)
    # sonde : boite blanche en polystyrene, antenne en dessous
    rounded(ctx, cx - 2.7, 40, 5.4, 6.2, 0.8)
    g = cairo.LinearGradient(cx - 2.7, 0, cx + 2.7, 0)
    g.add_color_stop_rgb(0, 1, 1, 1)
    g.add_color_stop_rgb(1, 0.82, 0.85, 0.89)
    ctx.set_source(g)
    ctx.fill_preserve()
    ctx.set_source_rgb(0.3, 0.32, 0.36)
    ctx.set_line_width(0.6)
    ctx.stroke()
    ctx.rectangle(cx - 1.6, 41.6, 3.2, 1.1)          # etiquette de la sonde
    ctx.set_source_rgb(0.2, 0.45, 0.8)
    ctx.fill()
    line(ctx, cx, 46.2, cx, 48, 0.5, (0.3, 0.32, 0.36))


def line(ctx, x0, y0, x1, y1, w, rgb=(0.45, 0.47, 0.5)):
    ctx.move_to(x0, y0)
    ctx.line_to(x1, y1)
    ctx.set_source_rgb(*rgb)
    ctx.set_line_width(w)
    ctx.stroke()


def rounded(ctx, x, y, w, h, r):
    ctx.new_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()


def render(scale):
    w, h = math.ceil(W * scale), math.ceil(H * scale)
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
    ctx = cairo.Context(surf)
    ctx.scale(scale, scale)
    draw(ctx)
    surf.flush()
    # cairo : BGRA premultiplie (petit-boutiste) -> RGBA
    return Image.frombuffer("RGBA", (w, h), bytes(surf.get_data()), "raw", "BGRa", surf.get_stride(), 1)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    render(WEB_SCALE).save(os.path.join(here, "web", "sonde.png"), optimize=True)
    im = render(SCREEN_SCALE)
    w, h = im.size
    px = im.load()
    vals = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            vals.append("0" if a == 0 else f"0x{a:02X}{r:02X}{g:02X}{b:02X}")
    with open(os.path.join(here, "md_sonde_icon.c"), "w") as out:
        out.write("// md_sonde_icon.c : icone de radiosonde de l'ecran, generee par make_sonde_icon.py (ne pas modifier).\n")
        out.write("// Pixels 0xAARRGGBB, 0 : transparent ; ancrage (position de la sonde) : centre de la boite.\n\n")
        out.write("#include <stdint.h>\n#include \"md.h\"\n\n")
        out.write(f"const int md_sonde_w = {w}, md_sonde_h = {h};\n")
        out.write(f"const float md_sonde_ax = {W / 2 * SCREEN_SCALE:.2f}f, md_sonde_ay = {BOX_CY * SCREEN_SCALE:.2f}f;\n\n")
        out.write(f"const uint32_t md_sonde_px[{w * h}] = {{\n")
        for k in range(0, len(vals), 12):
            out.write("  " + ",".join(vals[k:k + 12]) + ",\n")
        out.write("};\n")
    print(f"web/sonde.png {W * WEB_SCALE}x{H * WEB_SCALE}, md_sonde_icon.c {w}x{h}, ancrage boite y = {BOX_CY}")


if __name__ == "__main__":
    main()
