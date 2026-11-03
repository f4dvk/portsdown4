#!/usr/bin/env python3
"""Genere une police bitmap pour l'interface (meme format que process.py),
compatible avec Pillow 10 et plus (font.getsize n'existe plus).

Utilisation : process3.py fichier.ttf taille nom
  ecrit nom_taille.c et nom_taille.h dans le dossier courant, ex.
  process3.py /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf 20 dejavu_sans_bold
"""

import argparse
from PIL import Image, ImageFont, ImageDraw

parser = argparse.ArgumentParser()
parser.add_argument('font_file')
parser.add_argument('font_size', type=int)
parser.add_argument('font_name', help="Must be C variable-name-friendly")
args = parser.parse_args()

name = "%s_%d" % (args.font_name, args.font_size)
font = ImageFont.truetype(args.font_file, args.font_size)
ascent = font.getmetrics()[0]


def glyph(c):
    """Image du caractere, du haut des ascendantes au bas de l'encre, comme
    l'ancien font.getsize : largeur de la boite (debordement a gauche
    compris), hauteur jusqu'au bas des descendantes."""
    x0, _, x1, y1 = font.getbbox(c)
    x0 = min(0, x0)
    w, h = max(1, x1 - x0), max(1, y1)
    image = Image.new('L', (w, h), 255)
    ImageDraw.Draw(image).text((-x0, 0), c, font=font, fill=0)
    return image


digits_width = max(glyph(chr(v)).size[0] for v in range(48, 58))

out = ["#include <stdint.h>", "#include <stddef.h>", "#include \"font.h\""]
table = []
height = 0
for v in range(32, 127):
    image = glyph(chr(v))
    w, h = image.size
    height = max(height, h)
    out.append("static const uint8_t font_%s_character_%d[%d] = " % (name, v, w * h))
    out.append("{")
    rows = []
    for y in range(h):
        rows.append("    " + ", ".join("0x%02x" % (255 - image.getpixel((x, y))) for x in range(w)))
    out.append(",\n".join(rows))
    out.append("};")
    table.append("""
    {
        .width = %d,
        .height = %d,
        .render_width = %d,
        .map = font_%s_character_%d
    }""" % (w, h, digits_width if 48 <= v <= 57 else w, name, v))

empty = """
    {
        .height = 0,
        .width = 0,
        .render_width = 0,
        .map = NULL
    }"""
out.append("static const font_character_t font_%s_characters[] = {%s\n};"
           % (name, ",".join([empty] * 32 + table)))
out.append("""const font_t font_%s = {
    .height = %d,
    .ascent = %d,
    .characters = font_%s_characters
};""" % (name, height, ascent, name))

with open(name + ".c", "w") as f:
    f.write("\n".join(out) + "\n")

guard = "__FONT_%s_H__" % name.upper()
with open(name + ".h", "w") as f:
    f.write("#ifndef %s\n#define %s\n\nconst font_t font_%s;\n\n#endif /* %s */\n" % (guard, guard, name, guard))
print(name)
