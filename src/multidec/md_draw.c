// md_draw.c : dessin du decodeur multiple, avec le code de la GUI Portsdown
// (src/gui/Graphics.h, button3d.h et ses polices) : memes touches en relief,
// meme bandeau de titre. On dessine dans une image en memoire, recopiee d'un
// coup dans le framebuffer (pas de scintillement).

#include <math.h>
#include <stdbool.h>
#include "Graphics.h"
#include "button3d.h"
#include "md.h"

bool mouse_connected = false;           // attendu par Graphics.h

const md_rgb C_WHITE = {255, 255, 255};
const md_rgb C_HEAD  = {159, 179, 209};  // libelles secondaires (~10:1 sur noir)
const md_rgb C_GREEN = {125, 255, 154};
const md_rgb C_AMBER = {255, 192, 77};
const md_rgb C_RED   = {255, 110, 100};
const md_rgb C_GRID  = {42, 49, 64};
const md_rgb C_PANEL = {14, 18, 26};
const md_rgb C_DIM   = {30, 36, 48};
const md_rgb C_SLATE = {58, 75, 104};

static const font_t *FONTS[F_COUNT] = {
  &font_dejavu_sans_16, &font_dejavu_sans_18, &font_dejavu_sans_20, &font_dejavu_sans_22,
  &font_dejavu_sans_24, &font_dejavu_sans_cbold_16, &font_dejavu_sans_cbold_18,
  &font_dejavu_sans_cbold_20, &font_dejavu_sans_bold_16, &font_dejavu_sans_bold_18,
  &font_dejavu_sans_bold_20, &font_dejavu_sans_bold_22, &font_dejavu_sans_bold_24};

static char *realfb;
static int fb_line;                     // octets par ligne du framebuffer

int draw_init(void)
{
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  int fd = open("/dev/fb0", O_RDWR);

  screenXsize = MD_W;
  screenYsize = MD_H;
  fbp = calloc(MD_W * MD_H, 4);
  mfbp = fbp;
  if (fd < 0)
  {
    return -1;
  }
  if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) || ioctl(fd, FBIOGET_VSCREENINFO, &vinfo)
    || (vinfo.bits_per_pixel != 32) || (vinfo.xres < MD_W) || (vinfo.yres < MD_H))
  {
    fprintf(stderr, "framebuffer non gere (%d bits, %dx%d)\n", vinfo.bits_per_pixel, vinfo.xres, vinfo.yres);
    close(fd);
    return -1;
  }
  fb_line = finfo.line_length;
  realfb = mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (realfb == MAP_FAILED)
  {
    realfb = NULL;
    return -1;
  }
  return 0;
}

void draw_present(void)
{
  int y;
  if (!realfb)
  {
    return;
  }
  for (y = 0; y < MD_H; y++)
  {
    memcpy(realfb + y * fb_line, fbp + y * MD_W * 4, MD_W * 4);
  }
}

void draw_clear(void)
{
  memset(fbp, 0, MD_W * MD_H * 4);
}

// Titre et libelles passent par button3d.h, qui indexe lui aussi les polices
// par le code du caractere : ramenes a l'ASCII d'abord (un octet hors des
// polices fait planter le dessin)
void draw_title(const char *s)
{
  char t[128];
  snprintf(t, sizeof t, "%s", s);
  ascii_fold(t);
  b3d_title(t, 0);
}

static void colour_rgb(const char *name, int *r, int *g, int *b)
{
  static const struct { const char *n; int r, g, b; } C[] = {
    {"Blue", 0, 0, 128}, {"Green", 0, 128, 0}, {"Grey", 127, 127, 127}, {"DGrey", 63, 63, 63},
    {"Red", 255, 0, 0}, {"LBlue", 64, 64, 192}, {"Slate", 52, 68, 95}, {"Amber", 168, 93, 20},
    {"DRed", 147, 40, 31}};
  unsigned i;
  for (i = 0; i < sizeof C / sizeof C[0]; i++)
  {
    if (!strcmp(C[i].n, name))
    {
      *r = C[i].r; *g = C[i].g; *b = C[i].b;
      return;
    }
  }
  *r = 0; *g = 0; *b = 128;
}

// Touche Portsdown : label "ligne1^ligne2", big : grande police (une ligne)
void draw_button(int x, int y, int w, int h, const char *colour, const char *label, int big)
{
  char l1[64] = "", l2[64] = "";
  const char *p = strchr(label, '^');
  int r, g, b;
  colour_rgb(colour, &r, &g, &b);
  if (p)
  {
    snprintf(l1, sizeof l1, "%.*s", (int)(p - label), label);
    snprintf(l2, sizeof l2, "%s", p + 1);
  }
  else
  {
    snprintf(l1, sizeof l1, "%s", label);
  }
  ascii_fold(l1);
  ascii_fold(l2);
  b3d_button(x, y, w, h, r, g, b, l1, l2, B3D_PLAIN, 0, 128, 0, big && !p);
}

// Geometrie des menus a 25 touches de rpidatvtouch4.c (CreateButton)
void menu_rect(int pos, int *x, int *y, int *w, int *h)
{
  const int wb = 155, hb = 80;
  int yb;
  if (pos < 20)
  {
    *x = (pos % 5) * wb + 20; yb = (pos / 5) * hb + 20; *w = wb * 0.9; *h = hb * 0.9;
  }
  else if (pos == 20)
  {
    *x = 20; yb = 4 * hb + 20; *w = wb * 1.2; *h = hb * 1.2;
  }
  else
  {
    *x = ((pos + 1) % 5) * wb + 20; yb = (pos / 5) * hb + 20; *w = wb * 0.9; *h = hb * 1.2;
  }
  *y = 479 - yb - *h;
}

// Les polices de la GUI n'ont que les codes 0 a 126 : tout autre octet
// lirait hors du tableau des glyphes (plantage). Remplace par '?'.
static const font_character_t *glyph(const font_t *f, unsigned char c)
{
  if ((c < 32) || (c > 126))
  {
    c = '?';
  }
  return &f->characters[c];
}

int text_width(int font, const char *s)
{
  int w = 0;
  for (; *s; s++)
  {
    w += glyph(FONTS[font], (unsigned char)*s)->render_width;
  }
  return w;
}

// Nombre de caracteres de s qui tiennent dans maxw pixels
int text_fit(int font, const char *s, int maxw)
{
  int w = 0, n = 0;
  for (; s[n]; n++)
  {
    w += glyph(FONTS[font], (unsigned char)s[n])->render_width;
    if (w > maxw)
    {
      break;
    }
  }
  return n;
}

// Texte aligne a gauche (0), centre (1) ou a droite (2), ligne de base en y,
// melange avec ce qui est dessous
void draw_text(int x, int base, int font, md_rgb c, int align, const char *s)
{
  const font_t *f = FONTS[font];
  b3d_rgb col = {c.r, c.g, c.b};
  int w = text_width(font, s);
  const char *p;
  if (align == 1) x -= w / 2;
  if (align == 2) x -= w;
  for (p = s; *p; p++)
  {
    const font_character_t *ch = glyph(f, (unsigned char)*p);
    unsigned row, cl;
    for (row = 0; row < ch->height; row++)
    {
      int py = base + row - f->ascent;
      if ((py < 0) || (py >= MD_H)) continue;
      for (cl = 0; cl < ch->width; cl++)
      {
        int px = x + cl;
        float a = ch->map[cl + row * ch->width] / 255.0f;
        if ((a <= 0) || (px < 0) || (px >= MD_W)) continue;
        b3d_put(px, py, b3d_mix(b3d_get(px, py), col, a));
      }
    }
    x += ch->render_width;
  }
}

void draw_rect(int x, int y, int w, int h, md_rgb c)
{
  int i, j;
  for (j = y; j < y + h; j++)
  {
    if ((j < 0) || (j >= MD_H)) continue;
    for (i = x; i < x + w; i++)
    {
      if ((i < 0) || (i >= MD_W)) continue;
      unsigned char *p = (unsigned char *)fbp + (i + MD_W * j) * 4;
      p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = 0x80;
    }
  }
}

static void plot(int i, int j, md_rgb c, float a)
{
  b3d_rgb col = {c.r, c.g, c.b};
  if ((i >= 0) && (j >= 0) && (i < MD_W) && (j < MD_H))
  {
    b3d_put(i, j, b3d_mix(b3d_get(i, j), col, a));
  }
}

void draw_line(float x0, float y0, float x1, float y1, md_rgb c)
{
  float d = fmaxf(fabsf(x1 - x0), fabsf(y1 - y0));
  int n = (int)d + 1, k;
  for (k = 0; k <= n; k++)
  {
    plot((int)floorf(x0 + (x1 - x0) * k / n), (int)floorf(y0 + (y1 - y0) * k / n), c, 1);
  }
}

// Image w x h en pixels 0xAARRGGBB (0 : transparent), coin haut gauche en (x0, y0)
static void blit_rgba(const uint32_t *px, int w, int h, int x0, int y0)
{
  int i, j;
  for (j = 0; j < h; j++)
  {
    for (i = 0; i < w; i++)
    {
      uint32_t v = px[i + j * w];
      md_rgb c = {(v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF};
      if (v)
      {
        plot(x0 + i, y0 + j, c, (v >> 24) / 255.0f);
      }
    }
  }
}

// Symbole APRS centre en (cx, cy) : sym = table puis code ("/-" maison).
// Table '/' primaire, '\' alternative, chiffre ou majuscule : alternative
// avec ce caractere en surimpression (a-j : chiffres des positions
// compressees). Faux si le symbole n'est pas reconnu.
bool draw_aprs_symbol(int cx, int cy, const char *sym)
{
  int t, ov = 0;
  unsigned char tab = sym[0], code = tab ? sym[1] : 0;
  if ((code < 33) || (code > 126))
  {
    return false;
  }
  if (tab == '/') t = 0;
  else if (tab == '\\') t = 1;
  else if (((tab >= '0') && (tab <= '9')) || ((tab >= 'A') && (tab <= 'Z'))) { t = 1; ov = tab; }
  else if ((tab >= 'a') && (tab <= 'j')) { t = 1; ov = '0' + tab - 'a'; }
  else return false;
  cx -= MD_SYM / 2;
  cy -= MD_SYM / 2;
  blit_rgba(md_sym_px[t][code - 33], MD_SYM, MD_SYM, cx, cy);
  if (ov)
  {
    blit_rgba(md_sym_px[2][ov - 33], MD_SYM, MD_SYM, cx, cy);
  }
  return true;
}

// Avion vu de dessus, nez vers le haut (demi-envergure = 1, y vers le bas)
static const float PLANE[][2] = {
  {0, -1}, {0.12f, -0.82f}, {0.12f, -0.28f}, {1, 0.12f}, {1, 0.3f}, {0.12f, 0.08f}, {0.09f, 0.62f},
  {0.4f, 0.84f}, {0.4f, 0.98f}, {0, 0.88f}, {-0.4f, 0.98f}, {-0.4f, 0.84f}, {-0.09f, 0.62f},
  {-0.12f, 0.08f}, {-1, 0.3f}, {-1, 0.12f}, {-0.12f, -0.28f}, {-0.12f, -0.82f}};

static bool in_poly(const float (*p)[2], int n, float x, float y)
{
  bool in = false;
  int i, j;
  for (i = 0, j = n - 1; i < n; j = i++)
  {
    if (((p[i][1] > y) != (p[j][1] > y))
      && (x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]))
    {
      in = !in;
    }
  }
  return in;
}

// Avion de demi-envergure r pixels centre en (cx, cy), nez au cap hdg (degres,
// sens horaire depuis le nord) ; 4 x 4 echantillons par pixel pour lisser les bords
void draw_plane(float cx, float cy, float r, float hdg, md_rgb c)
{
  const int n = sizeof PLANE / sizeof PLANE[0], ss = 4;
  float sn = sinf(hdg * (float)M_PI / 180), cs = cosf(hdg * (float)M_PI / 180);
  int i, j, a, b;
  for (j = (int)floorf(cy - r - 1); j <= (int)(cy + r + 1); j++)
  {
    for (i = (int)floorf(cx - r - 1); i <= (int)(cx + r + 1); i++)
    {
      int hits = 0;
      for (b = 0; b < ss; b++)
      {
        for (a = 0; a < ss; a++)
        {
          // point de l'ecran ramene dans le repere de l'avion (rotation inverse)
          float dx = (i + (a + 0.5f) / ss - cx) / r, dy = (j + (b + 0.5f) / ss - cy) / r;
          hits += in_poly(PLANE, n, dx * cs + dy * sn, dy * cs - dx * sn);
        }
      }
      if (hits)
      {
        plot(i, j, c, hits / (float)(ss * ss));
      }
    }
  }
}

// Radiosonde (md_sonde_icon.c) : ballon, parachute et sonde, le centre de la
// boite de la sonde en (x, y)
void draw_sonde_icon(float x, float y)
{
  blit_rgba(md_sonde_px, md_sonde_w, md_sonde_h, (int)floorf(x - md_sonde_ax + 0.5f),
            (int)floorf(y - md_sonde_ay + 0.5f));
}

void draw_circle(float cx, float cy, float r, md_rgb c, int fill)
{
  int i, j;
  for (j = (int)(cy - r - 1); j <= (int)(cy + r + 1); j++)
  {
    for (i = (int)(cx - r - 1); i <= (int)(cx + r + 1); i++)
    {
      float d = sqrtf((i + 0.5f - cx) * (i + 0.5f - cx) + (j + 0.5f - cy) * (j + 0.5f - cy));
      float a = fill ? b3d_clamp(r + 0.5f - d, 0, 1) : b3d_clamp(1.0f - fabsf(d - r), 0, 1);
      if (a > 0)
      {
        plot(i, j, c, a);
      }
    }
  }
}
