/*******************************************************************************
 * button3d.h : touches de l'interface en relief 3D, dessinees directement dans
 * le framebuffer (32 bits, BGRA, origine en haut a gauche).
 *
 * Tout est dessine a l'interieur du rectangle de la touche, sans deborder (le
 * clavier n'a qu'un pixel entre deux touches) :
 *  - autour du corps arrondi, le fond deja present a l'ecran (blanc, noir ou
 *    panneau gris), lu aux deux coins gauches de la touche : ces pixels restent
 *    du fond quand la touche est redessinee ;
 *  - corps en degrade vertical, reflet sur la moitie haute, lisere clair, et
 *    ombre portee de B3D_SHADOW pixels en bas ;
 *  - une touche desactivee (gris) est plate, sans ombre ni reflet ;
 *  - en mode "valeur" (menu principal, parametres du recepteur), le libelle
 *    est en haut et la valeur dans un afficheur en creux ;
 *  - le texte est melange au pixel pres avec ce qui est dessous ; un texte trop
 *    large passe en police condensee, puis plus petite.
 ******************************************************************************/

#ifndef __BUTTON3D_H__
#define __BUTTON3D_H__

#include <math.h>

// Le GUI est compile sans optimisation : le calcul pixel par pixel des
// touches est optimise ici seulement.
#pragma GCC push_options
#pragma GCC optimize ("O2")

#define B3D_SHADOW 3          // hauteur de l'ombre sous le corps (pixels)

#define B3D_PLAIN 0           // une ou deux lignes centrees
#define B3D_VALUE 1           // libelle en haut, valeur dans l'afficheur

typedef struct { float r, g, b; } b3d_rgb;

static const font_t *b3d_fonts_big[] = {&font_dejavu_sans_bold_28, &font_dejavu_sans_cbold_28,
  &font_dejavu_sans_cbold_22, &font_dejavu_sans_cbold_20, NULL};
static const font_t *b3d_fonts_tall2[] = {&font_dejavu_sans_bold_22, &font_dejavu_sans_cbold_22,
  &font_dejavu_sans_cbold_20, &font_dejavu_sans_cbold_18, NULL};
static const font_t *b3d_fonts_mid[] = {&font_dejavu_sans_bold_20, &font_dejavu_sans_cbold_20,
  &font_dejavu_sans_cbold_18, &font_dejavu_sans_cbold_16, NULL};
static const font_t *b3d_fonts_label[] = {&font_dejavu_sans_16, &font_dejavu_sans_cbold_16, NULL};


static b3d_rgb b3d_hex(unsigned int v)
{
  b3d_rgb c = {(v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF};
  return c;
}

static b3d_rgb b3d_mix(b3d_rgb a, b3d_rgb b, float t)
{
  b3d_rgb c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
  return c;
}

static float b3d_clamp(float v, float lo, float hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

// Pixel du fond deja affiche : on lit l'image sans le curseur de la souris
// quand elle est branchee (le curseur est dessine dans fbp).
static b3d_rgb b3d_read_bg(int x, int y)
{
  b3d_rgb c = {0, 0, 0};
  char *buf = mouse_connected ? mfbp : fbp;
  if ((x < 0) || (y < 0) || (x >= screenXsize) || (y >= screenYsize))
  {
    return c;
  }
  unsigned char *p = (unsigned char *)buf + (x + screenXsize * y) * 4;
  c.b = p[0];
  c.g = p[1];
  c.r = p[2];
  return c;
}

static b3d_rgb b3d_get(int x, int y)
{
  b3d_rgb c = {0, 0, 0};
  unsigned char *p = (unsigned char *)fbp + (x + screenXsize * y) * 4;
  c.b = p[0];
  c.g = p[1];
  c.r = p[2];
  return c;
}

static void b3d_put(int x, int y, b3d_rgb c)
{
  if ((x < 0) || (y < 0) || (x >= screenXsize) || (y >= screenYsize))
  {
    return;
  }
  unsigned char *p = (unsigned char *)fbp + (x + screenXsize * y) * 4;
  p[0] = (unsigned char)b3d_clamp(c.b + 0.5, 0, 255);
  p[1] = (unsigned char)b3d_clamp(c.g + 0.5, 0, 255);
  p[2] = (unsigned char)b3d_clamp(c.r + 0.5, 0, 255);
  p[3] = 0x80;
}

// Distance signee au bord d'un rectangle arrondi (negative a l'interieur),
// pour le centre du pixel (px, py).
static float b3d_sdf(float px, float py, float x, float y, float w, float h, float r)
{
  float hx = w / 2, hy = h / 2;
  float qx = fabsf(px + 0.5f - (x + hx)) - (hx - r);
  float qy = fabsf(py + 0.5f - (y + hy)) - (hy - r);
  float ox = qx > 0 ? qx : 0;
  float oy = qy > 0 ? qy : 0;
  float in = (qx > qy ? qx : qy);
  return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

// Degrade a trois couleurs (haut, milieu a 55 %, bas)
static b3d_rgb b3d_grad3(const b3d_rgb pal[3], float t)
{
  if (t < 0.55f)
  {
    return b3d_mix(pal[0], pal[1], t / 0.55f);
  }
  return b3d_mix(pal[1], pal[2], (t - 0.55f) / 0.45f);
}

// Couleurs du relief a partir de la couleur d'origine de la touche.
// Retourne 1 si la touche est desactivee (gris).
static int b3d_palette(int r, int g, int b, b3d_rgb pal[3])
{
  static const struct { int r, g, b; unsigned int top, mid, bot; } known[] = {
    {0,   0,   128, 0x3d63e6, 0x1b36a8, 0x0f2170},   // Blue
    {0,   128, 0,   0x3fd16a, 0x1a9a3f, 0x0d6427},   // Green
    {255, 0,   0,   0xff6b5e, 0xe3241b, 0x9c0f0b},   // Red (TX ON)
    {64,  64,  192, 0x7f9cff, 0x4a68e0, 0x2a44b0},   // LBlue
    {0,   0,   64,  0x5c6f8f, 0x34445f, 0x1f2a3d},   // DBlue (Cancel)
    {52,  68,  95,  0x5c6f8f, 0x34445f, 0x1f2a3d},   // Slate (navigation)
    {147, 40,  31,  0xc4524b, 0x93281f, 0x5e1410},   // DRed (TX au repos)
    {168, 93,  20,  0xd98a3a, 0xa85d14, 0x6e3a08},   // Amber (arret, redemarrage)
  };
  unsigned int i;

  if (((r == 127) && (g == 127) && (b == 127)) || ((r == 63) && (g == 63) && (b == 63)))
  {
    return 1;
  }
  for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
  {
    if ((known[i].r == r) && (known[i].g == g) && (known[i].b == b))
    {
      pal[0] = b3d_hex(known[i].top);
      pal[1] = b3d_hex(known[i].mid);
      pal[2] = b3d_hex(known[i].bot);
      return 0;
    }
  }
  // Autre couleur : eclaircie en haut, assombrie en bas
  b3d_rgb c = {r, g, b};
  b3d_rgb white = {255, 255, 255};
  b3d_rgb black = {0, 0, 0};
  pal[0] = b3d_mix(c, white, 0.30f);
  pal[1] = c;
  pal[2] = b3d_mix(c, black, 0.40f);
  return 0;
}

// Premiere police de la liste dans laquelle le texte tient en largeur
static const font_t *b3d_fit(const font_t **list, char *s, int maxw)
{
  int i;
  for (i = 0; list[i + 1] != NULL; i++)
  {
    if ((int)font_width_string(list[i], s) <= maxw)
    {
      break;
    }
  }
  return list[i];
}

// Texte centre sur cx, ligne de base en y, melange avec l'image dessous.
// shadow : opacite d'une ombre portee d'un pixel (0 : aucune)
static void b3d_text(int cx, int y, char *s, const font_t *f, b3d_rgb col, float shadow)
{
  int pass;
  for (pass = (shadow > 0 ? 0 : 1); pass < 2; pass++)
  {
    b3d_rgb c = pass ? col : (b3d_rgb){0, 0, 0};
    float opacity = pass ? 1.0f : shadow;
    int x = cx - (int)font_width_string(f, s) / 2;
    int dy = pass ? 0 : 1;
    char *p;
    for (p = s; *p; p++)
    {
      const font_character_t *ch = &f->characters[(uint8_t)*p];
      unsigned int row, colm;
      for (row = 0; row < ch->height; row++)
      {
        int py = y + dy + row - f->ascent;
        if ((py < 0) || (py >= screenYsize))
        {
          continue;
        }
        for (colm = 0; colm < ch->width; colm++)
        {
          int px = x + colm;
          float a = ch->map[colm + row * ch->width] / 255.0f * opacity;
          if ((a <= 0) || (px < 0) || (px >= screenXsize))
          {
            continue;
          }
          b3d_put(px, py, b3d_mix(b3d_get(px, py), c, a));
        }
      }
      x += ch->render_width;
    }
  }
}

// Copie sans les espaces de debut et de fin
static void b3d_trim(char *dst, const char *src, int size)
{
  int n;
  while (*src == ' ')
  {
    src++;
  }
  snprintf(dst, size, "%s", src);
  n = strlen(dst);
  while ((n > 0) && (dst[n - 1] == ' '))
  {
    dst[--n] = '\0';
  }
}

// Ligne de base pour centrer verticalement un texte (hauteur des majuscules)
static int b3d_baseline(const font_t *f, float centre_y)
{
  return (int)(centre_y + f->ascent * 0.73f / 2.0f + 0.5f);
}

/*******************************************************************************
 * Dessine une touche.
 *  x, top, w, h : rectangle de la touche (origine en haut a gauche de l'ecran)
 *  r, g, b      : couleur d'origine de la touche (Blue, Green, Grey...)
 *  line1, line2 : texte (line2 vide : une seule ligne)
 *  mode         : B3D_PLAIN, ou B3D_VALUE (line1 libelle, line2 valeur)
 *  value_rgb    : couleur d'etat de la valeur en mode B3D_VALUE (Green :
 *                 valeur normale, Grey : appareil absent...)
 *  big          : grande police pour une ligne seule (menus principaux, clavier)
 ******************************************************************************/
void b3d_button(int x, int top, int w, int h, int r, int g, int b,
                const char *line1, const char *line2, int mode,
                int value_r, int value_g, int value_b, int big)
{
  b3d_rgb pal[3];
  b3d_rgb lcdpal[2];
  b3d_rgb white = {255, 255, 255};
  b3d_rgb black = {0, 0, 0};
  int disabled = b3d_palette(r, g, b, pal);
  int i, j;
  char l1[64], l2[64];

  // Fond autour de la touche : les deux coins gauches (hors du corps arrondi)
  b3d_rgb bg_top = b3d_read_bg(x, top);
  b3d_rgb bg_bot = b3d_read_bg(x, top + h - 1);
  int light = (bg_top.r + bg_top.g + bg_top.b) > 3 * 160;

  if (disabled)
  {
    if ((r == 63) && (g == 63))      // DGrey : plus sombre
    {
      pal[0] = b3d_hex(0x6a6a6a);
      pal[1] = pal[2] = b3d_hex(0x5c5c5c);
    }
    else if (light)
    {
      pal[0] = b3d_hex(0xb9c1cc);
      pal[1] = pal[2] = b3d_hex(0xa9b2be);
    }
    else
    {
      pal[0] = b3d_hex(0x9a9a9a);
      pal[1] = pal[2] = b3d_hex(0x8a8a8a);
    }
  }

  // Geometrie : corps arrondi, ombre dessous
  float rad = (h < 64) ? 8 : 10;
  float bx = x + 1, by = top + 1, bw = w - 2, bh = h - 2 - B3D_SHADOW;
  if (disabled)
  {
    bh = h - 2;
  }

  // Afficheur de la valeur
  float lx = bx + 6, ly = by + bh * 0.45f, lw = bw - 12, lh = bh * 0.47f;
  int lcd = (mode == B3D_VALUE);
  if (disabled)
  {
    lcdpal[0] = b3d_hex(light ? 0x8d95a0 : 0x7c7c7c);
    lcdpal[1] = b3d_hex(light ? 0x9aa2ad : 0x888888);
  }
  else
  {
    lcdpal[0] = b3d_hex(0x1d2a1f);
    lcdpal[1] = b3d_hex(0x2c3d2e);
  }

  for (j = top; j < top + h; j++)
  {
    b3d_rgb bg = b3d_mix(bg_top, bg_bot, (h > 1) ? (float)(j - top) / (h - 1) : 0);
    float t = b3d_clamp((j - by) / (bh > 1 ? bh - 1 : 1), 0, 1);
    b3d_rgb body = disabled ? b3d_mix(pal[0], pal[1], t) : b3d_grad3(pal, t);
    for (i = x; i < x + w; i++)
    {
      b3d_rgb c = bg;
      float d = b3d_sdf(i, j, bx, by, bw, bh, rad);
      float cover = b3d_clamp(0.5f - d, 0, 1);

      // Ombre portee, un peu floue (cachee sous le corps opaque)
      if ((!disabled) && (cover < 1))
      {
        float ds = b3d_sdf(i, j, bx, by + B3D_SHADOW, bw, bh, rad);
        float cs = b3d_clamp((1.5f - ds) / 3.0f, 0, 1) * 0.35f;
        c = b3d_mix(c, black, cs);
      }

      if (cover > 0)
      {
        b3d_rgb bc = body;
        if (!disabled)
        {
          // Reflet sur la moitie haute
          float sheen_h = bh * 0.5f;
          float csh = 0;
          if (j < by + 2 + sheen_h + 1)
          {
            csh = b3d_clamp(0.5f - b3d_sdf(i, j, bx + 3, by + 2, bw - 6, sheen_h, rad - 2), 0, 1);
          }
          if (csh > 0)
          {
            float u = b3d_clamp((j - (by + 2)) / sheen_h, 0, 1);
            bc = b3d_mix(bc, white, csh * (0.35f + (0.03f - 0.35f) * u));
          }
          // Lisere clair sur le pourtour
          bc = b3d_mix(bc, white, 0.27f * b3d_clamp(d + 1.5f, 0, 1));
        }
        else
        {
          bc = b3d_mix(bc, black, 0.18f * b3d_clamp(d + 1.5f, 0, 1));
        }

        // Afficheur en creux
        if (lcd && (j >= ly - 1) && (j <= ly + lh + 1) && (i >= lx - 1) && (i <= lx + lw + 1))
        {
          float dl = b3d_sdf(i, j, lx, ly, lw, lh, 6);
          float cl = b3d_clamp(0.5f - dl, 0, 1);
          if (cl > 0)
          {
            float v = b3d_clamp((j - ly) / lh, 0, 1);
            b3d_rgb lc = b3d_mix(lcdpal[0], lcdpal[1], v);
            float depth = -dl;                            // distance au bord
            lc = b3d_mix(lc, black, 0.55f * b3d_clamp(1.0f - depth / 2.5f, 0, 1));
            bc = b3d_mix(bc, lc, cl);
          }
        }
        c = b3d_mix(c, bc, cover);
      }
      b3d_put(i, j, c);
    }
  }

  // Texte
  b3d_trim(l1, line1 ? line1 : "", sizeof(l1));
  b3d_trim(l2, line2 ? line2 : "", sizeof(l2));
  b3d_rgb txt = white;
  float shadow = 0.43f;
  if (disabled)
  {
    txt = light ? (b3d_rgb){95, 103, 115} : (b3d_rgb){60, 60, 60};
    shadow = 0;
  }

  if (lcd)
  {
    const font_t *fl = b3d_fit(b3d_fonts_label, l1, bw - 10);
    const font_t *fv = b3d_fit(b3d_fonts_mid, l2, lw - 8);
    b3d_rgb label = disabled ? txt : (b3d_rgb){230, 236, 255};
    b3d_rgb value;
    if (disabled || ((value_r == 127) && (value_g == 127) && (value_b == 127))
     || ((value_r == 63) && (value_g == 63) && (value_b == 63)))
    {
      value = (b3d_rgb){110, 118, 128};             // valeur indisponible
    }
    else if ((value_r == 0) && (value_g == 128) && (value_b == 0))
    {
      value = b3d_hex(0x7dff9a);                    // valeur normale
    }
    else
    {
      value = b3d_mix((b3d_rgb){value_r, value_g, value_b}, white, 0.35f);
    }
    b3d_text(bx + bw / 2, b3d_baseline(fl, by + (ly - by) / 2 + 1), l1, fl, label, disabled ? 0 : 0.35f);
    b3d_text(lx + lw / 2, b3d_baseline(fv, ly + lh / 2 + 1), l2, fv, value, 0);
    return;
  }

  if ((l1[0] != '\0') && (l2[0] != '\0'))           // deux lignes
  {
    const font_t **list = (h >= 90) ? b3d_fonts_tall2 : b3d_fonts_mid;
    const font_t *f1 = b3d_fit(list, l1, bw - 12);
    const font_t *f2 = b3d_fit(list, l2, bw - 12);
    float step = list[0]->ascent * 1.25f;
    float cy = by + bh / 2;
    b3d_text(bx + bw / 2, b3d_baseline(f1, cy - step / 2), l1, f1, txt, shadow);
    b3d_text(bx + bw / 2, b3d_baseline(f2, cy + step / 2), l2, f2, txt, shadow);
  }
  else                                              // une ligne
  {
    char *s = (l1[0] != '\0') ? l1 : l2;
    const font_t *f = b3d_fit(big ? b3d_fonts_big : b3d_fonts_mid, s, bw - 12);
    b3d_text(bx + bw / 2, b3d_baseline(f, by + bh / 2), s, f, txt, shadow);
  }
}

/*******************************************************************************
 * Panneau gris des sous-menus de choix, arrondi et en leger relief.
 * x, top, w, h : rectangle (origine en haut a gauche de l'ecran)
 ******************************************************************************/
void b3d_panel(int x, int top, int w, int h)
{
  b3d_rgb bg_top = b3d_read_bg(x, top);
  b3d_rgb bg_bot = b3d_read_bg(x, top + h - 1);
  b3d_rgb hi = b3d_hex(0x8c8c8c), lo = b3d_hex(0x727272);
  b3d_rgb white = {255, 255, 255};
  b3d_rgb black = {0, 0, 0};
  int i, j;

  for (j = top; j < top + h; j++)
  {
    b3d_rgb bg = b3d_mix(bg_top, bg_bot, (h > 1) ? (float)(j - top) / (h - 1) : 0);
    b3d_rgb body = b3d_mix(hi, lo, (float)(j - top) / (h > 1 ? h - 1 : 1));
    for (i = x; i < x + w; i++)
    {
      float d = b3d_sdf(i, j, x, top, w, h, 16);
      float cover = b3d_clamp(0.5f - d, 0, 1);
      b3d_rgb c = bg;
      if (cover > 0)
      {
        b3d_rgb pc = b3d_mix(body, white, 0.27f * b3d_clamp(d + 1.5f, 0, 1));  // lisere clair
        pc = b3d_mix(pc, black, 0.35f * b3d_clamp(1.0f - (-d - 1.5f) / 3.0f, 0, 1)
                                 * (d < -1.5f ? 1 : 0));                         // bord interieur sombre
        c = b3d_mix(c, pc, cover);
      }
      b3d_put(i, j, c);
    }
  }
}

#pragma GCC pop_options

#endif /* __BUTTON3D_H__ */
