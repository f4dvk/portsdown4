#!/usr/bin/env python3
# make_aprs_symbols.py : genere md_aprs_sym.c (icones APRS du radar) a partir
# des planches PNG du jeu de symboles d'aprs.fi (Heikki Hannikainen OH7LZB,
# https://github.com/hessu/aprs-symbols, libre d'usage en citant la source).
#
#   git clone https://github.com/hessu/aprs-symbols
#   python3 make_aprs_symbols.py aprs-symbols/png > md_aprs_sym.c
#
# A lancer sur un PC (Pillow) : le fichier genere est garde dans le depot, le
# Pi n'a besoin que du compilateur C. La taille doit valoir MD_SYM (md.h).

import sys
from PIL import Image

SIZE = 24                         # pixels par icone (MD_SYM)
COLS = 16                         # planche : 16 x 6 cases, code ASCII 33 ('!') en premier
NCODES = 94                       # codes 33 a 126
TABLES = ("primary table /", "alternate table (backslash)", "overlay characters")   # pas de \ en fin de commentaire C


def sheet(pngdir, table):
    # Planche native a la bonne taille, sinon reduction de la planche 64 px
    try:
        im = Image.open(f"{pngdir}/aprs-symbols-{SIZE}-{table}.png")
    except FileNotFoundError:
        im = Image.open(f"{pngdir}/aprs-symbols-64-{table}.png")
        im = im.resize((COLS * SIZE, 6 * SIZE), Image.LANCZOS)
    im = im.convert("RGBA")
    if im.size != (COLS * SIZE, 6 * SIZE):
        sys.exit(f"planche {table} : taille inattendue {im.size}")
    return im


def main():
    if len(sys.argv) != 2:
        sys.exit("usage : make_aprs_symbols.py <dossier png de aprs-symbols> > md_aprs_sym.c")
    out = sys.stdout
    out.write("// md_aprs_sym.c : icones APRS du radar, genere par make_aprs_symbols.py (ne pas modifier).\n")
    out.write("// Jeu de symboles d'aprs.fi, Heikki Hannikainen OH7LZB : https://github.com/hessu/aprs-symbols\n")
    out.write("// md_sym_px[table][code - 33][pixel] : table 0 primaire '/', 1 alternative '\\\\',\n")
    out.write(f"// 2 caracteres d'overlay ; {SIZE} x {SIZE} pixels 0xAARRGGBB, 0 : transparent.\n\n")
    out.write("#include <stdint.h>\n#include \"md.h\"\n\n")
    out.write(f"#if MD_SYM != {SIZE}\n#error \"MD_SYM (md.h) et make_aprs_symbols.py ne sont pas d'accord\"\n#endif\n\n")
    out.write("const uint32_t md_sym_px[3][94][MD_SYM * MD_SYM] = {\n")
    for t in range(3):
        im = sheet(sys.argv[1], t)
        px = im.load()
        out.write(f"  {{ // {TABLES[t]}\n")
        for code in range(NCODES):
            x0, y0 = (code % COLS) * SIZE, (code // COLS) * SIZE
            vals = []
            for y in range(SIZE):
                for x in range(SIZE):
                    r, g, b, a = px[x0 + x, y0 + y]
                    vals.append("0" if a == 0 else f"0x{a:02X}{r:02X}{g:02X}{b:02X}")
            ch = chr(33 + code)
            out.write(f"    {{ // {ch if ch != chr(92) else 'backslash'}\n")
            for k in range(0, len(vals), 12):
                out.write("      " + ",".join(vals[k:k + 12]) + ",\n")
            out.write("    },\n")
        out.write("  },\n")
    out.write("};\n")


if __name__ == "__main__":
    main()
