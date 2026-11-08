// md_util.c : configuration, lecture JSON, messages et objets decodes,
// distances et locators, journal du decodeur multiple.

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "md.h"

// ---------------------------------------------------------------- configuration

#define CONF_MAX 256
static char conf_k[CONF_MAX][48], conf_v[CONF_MAX][64];
static int conf_n;

void conf_load(void)
{
  char ln[160];
  FILE *f = fopen(MD_CONFIG, "r");
  conf_n = 0;
  if (!f)
  {
    return;
  }
  while (fgets(ln, sizeof ln, f) && (conf_n < CONF_MAX))
  {
    char *eq = strchr(ln, '=');
    if ((ln[0] == '#') || !eq)
    {
      continue;
    }
    *eq = 0;
    ln[strcspn(ln, " \t")] = 0;
    eq[1 + strcspn(eq + 1, "\r\n")] = 0;
    snprintf(conf_k[conf_n], sizeof conf_k[0], "%s", ln);
    snprintf(conf_v[conf_n], sizeof conf_v[0], "%s", eq + 1);
    conf_n++;
  }
  fclose(f);
}

const char *conf_get(const char *key, const char *def)
{
  int i;
  for (i = 0; i < conf_n; i++)
  {
    if (!strcmp(conf_k[i], key))
    {
      return conf_v[i];
    }
  }
  return def;
}

void conf_set(const char *key, const char *val)
{
  char tmp[160];
  FILE *f;
  int i;
  for (i = 0; i < conf_n; i++)
  {
    if (!strcmp(conf_k[i], key))
    {
      break;
    }
  }
  if (i == conf_n)
  {
    if (conf_n == CONF_MAX)
    {
      return;
    }
    snprintf(conf_k[conf_n++], sizeof conf_k[0], "%s", key);
  }
  snprintf(conf_v[i], sizeof conf_v[0], "%s", val);

  snprintf(tmp, sizeof tmp, "%s.tmp", MD_CONFIG);
  f = fopen(tmp, "w");
  if (!f)
  {
    return;
  }
  fprintf(f, "# Decodeur multiple (multidec) : reglages, ecrits par l'application\n");
  for (i = 0; i < conf_n; i++)
  {
    fprintf(f, "%s=%s\n", conf_k[i], conf_v[i]);
  }
  fclose(f);
  rename(tmp, MD_CONFIG);
}

// Lecture tolerante d'un fichier de configuration Portsdown (cle absente : out inchange)
void pconf_get(const char *file, const char *key, char *out, int size)
{
  char ln[256];
  int kl = strlen(key);
  FILE *f = fopen(file, "r");
  if (!f)
  {
    return;
  }
  while (fgets(ln, sizeof ln, f))
  {
    if (!strncmp(ln, key, kl) && (ln[kl] == '='))
    {
      ln[strcspn(ln, "\r\n")] = 0;
      snprintf(out, size, "%s", ln + kl + 1);
      break;
    }
  }
  fclose(f);
}

// ---------------------------------------------------------------- JSON

static const char *ws(const char *p)
{
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

// Pointeur juste apres la valeur JSON qui commence en p
const char *json_skip(const char *p)
{
  int depth = 0;
  bool str = false;
  p = ws(p);
  if ((*p != '{') && (*p != '[') && (*p != '"'))
  {
    while (*p && (*p != ',') && (*p != '}') && (*p != ']') && !isspace((unsigned char)*p)) p++;
    return p;
  }
  for (; *p; p++)
  {
    if (str)
    {
      if (*p == '\\' && p[1]) p++;
      else if (*p == '"') { str = false; if (depth == 0) return p + 1; }
      continue;
    }
    if (*p == '"') str = true;
    else if ((*p == '{') || (*p == '[')) depth++;
    else if ((*p == '}') || (*p == ']'))
    {
      if (--depth == 0) return p + 1;
    }
  }
  return p;
}

// Valeur de la cle key (longueur kl) dans l'objet qui commence en obj
static const char *obj_key(const char *obj, const char *key, int kl)
{
  const char *p = ws(obj);
  if (*p != '{')
  {
    return NULL;
  }
  p++;
  while (*(p = ws(p)) == '"')
  {
    const char *k = p + 1, *e = k;
    while (*e && (*e != '"')) { if (*e == '\\' && e[1]) e++; e++; }
    if (!*e) return NULL;
    p = ws(e + 1);
    if (*p != ':') return NULL;
    p = ws(p + 1);
    if (((e - k) == kl) && !strncmp(k, key, kl))
    {
      return p;
    }
    p = ws(json_skip(p));
    if (*p == ',') p++;
  }
  return NULL;
}

// Valeur au chemin "a.b.c" ; *len : longueur du texte de la valeur
const char *json_find(const char *js, const char *path, int *len)
{
  const char *p = js;
  while (p && *path)
  {
    const char *dot = strchr(path, '.');
    int kl = dot ? (int)(dot - path) : (int)strlen(path);
    p = obj_key(p, path, kl);
    path += kl + (dot ? 1 : 0);
  }
  if (p && len)
  {
    *len = json_skip(p) - p;
  }
  return p;
}

bool json_str(const char *js, const char *path, char *out, int size)
{
  int len, o = 0;
  const char *p = json_find(js, path, &len);
  if (!p || !strncmp(p, "null", 4) || (size < 2))
  {
    return false;
  }
  if (*p != '"')
  {
    snprintf(out, size, "%.*s", len, p);
    return true;
  }
  for (p++; *p && (*p != '"') && (o < size - 1); p++)
  {
    if ((*p == '\\') && p[1])
    {
      p++;
      if (*p == 'n' || *p == 'r' || *p == 't') out[o++] = ' ';
      else if (*p == 'u' && isxdigit((unsigned char)p[1]))
      {
        unsigned v = (unsigned)strtoul((char[]){p[1], p[2], p[3], p[4], 0}, NULL, 16);
        out[o++] = (v >= 32 && v < 256) ? (char)v : '?';
        p += 4;
      }
      else out[o++] = *p;
    }
    else
    {
      out[o++] = *p;
    }
  }
  out[o] = 0;
  return true;
}

bool json_num(const char *js, const char *path, double *v)
{
  char s[40];
  if (!json_str(js, path, s, sizeof s))
  {
    return false;
  }
  if (!strcmp(s, "true")) { *v = 1; return true; }
  if (!strcmp(s, "false")) { *v = 0; return true; }
  char *end;
  *v = strtod(s, &end);
  return end != s;
}

// ---------------------------------------------------------------- donnees

md_msg g_msg[MSG_MAX];
int g_nmsg, g_msghead;
md_obj g_obj[OBJ_MAX];
int g_nobj;
md_track g_track[TRACK_MAX];
int g_ntrack;
char g_track_id[24];
char g_status[128];
double g_slat = NAN, g_slon = NAN;
bool g_dirty = true;
char g_stream[60][80];
int g_stream_lines = 1, g_stream_col;
static int stream_px;

// Texte decode ramene a l'ASCII affichable par les polices de la GUI :
// lettres accentuees (UTF-8 ou Latin-1) -> lettre de base, autres -> '?'
void ascii_fold(char *s)
{
  static const char l1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";   // 0xC0 a 0xFF
  unsigned char *p = (unsigned char *)s, *o = (unsigned char *)s;
  while (*p)
  {
    unsigned c = *p++;
    if (c < 0x80)
    {
      *o++ = ((c < 32) && (c != 0)) ? ' ' : (c == 127 ? '?' : c);
      continue;
    }
    if ((c == 0xC3) && (*p >= 0x80) && (*p <= 0xBF))          // UTF-8 de U+00C0 a U+00FF
    {
      *o++ = l1[*p++ - 0x80];
      continue;
    }
    if ((c >= 0xC2) && (c <= 0xF4) && (*p >= 0x80) && (*p <= 0xBF))   // autre UTF-8 : un '?'
    {
      while ((*p >= 0x80) && (*p <= 0xBF)) p++;
      *o++ = '?';
      continue;
    }
    *o++ = (c >= 0xC0) ? l1[c - 0xC0] : '?';                   // Latin-1
  }
  *o = 0;
}

void msg_add(md_rgb col, const char *head, const char *body)
{
  md_msg *m = &g_msg[g_msghead];
  m->t = time(NULL);
  m->col = col;
  snprintf(m->head, sizeof m->head, "%s", head);
  snprintf(m->body, sizeof m->body, "%s", body);
  ascii_fold(m->head);
  ascii_fold(m->body);
  g_msghead = (g_msghead + 1) % MSG_MAX;
  if (g_nmsg < MSG_MAX) g_nmsg++;
  g_dirty = true;
}

md_obj *obj_get(int type, const char *rawid)
{
  int i, old = 0;
  md_obj *o;
  char id[24];
  snprintf(id, sizeof id, "%s", rawid);
  ascii_fold(id);                        // cle comparee et affichee en ASCII
  for (i = 0; i < g_nobj; i++)
  {
    if ((g_obj[i].type == type) && !strcmp(g_obj[i].id, id))
    {
      g_obj[i].seen = time(NULL);
      g_obj[i].count++;
      return &g_obj[i];
    }
    if (g_obj[i].seen < g_obj[old].seen) old = i;
  }
  o = (g_nobj < OBJ_MAX) ? &g_obj[g_nobj++] : &g_obj[old];
  memset(o, 0, sizeof *o);
  o->type = type;
  snprintf(o->id, sizeof o->id, "%s", id);
  o->first = o->seen = time(NULL);
  o->count = 1;
  o->alt = o->spd = o->crs = o->vz = o->temp = o->hum = o->batt = o->snr = NAN;
  o->burst = -1;
  return o;
}

void obj_pos(md_obj *o, double lat, double lon)
{
  if (!isfinite(lat) || !isfinite(lon) || ((lat == 0) && (lon == 0)) || fabs(lat) > 90 || fabs(lon) > 180)
  {
    return;
  }
  if (!o->pos || (o->ntrail == 0) || (fabs(o->trail[(o->ntrail - 1) % TRAIL][0] - lat) > 1e-4)
    || (fabs(o->trail[(o->ntrail - 1) % TRAIL][1] - lon) > 1e-4))
  {
    o->trail[o->ntrail % TRAIL][0] = lat;
    o->trail[o->ntrail % TRAIL][1] = lon;
    o->ntrail++;
  }
  o->lat = lat;
  o->lon = lon;
  o->pos = true;
}

// Trajectoire de la radiosonde suivie (la plus recente, tant qu'elle est entendue)
void track_add(const char *id, double lat, double lon, double alt)
{
  static time_t last;
  time_t now = time(NULL);
  if (strcmp(id, g_track_id))
  {
    if (g_track_id[0] && (now - last < 60))
    {
      return;                            // une autre sonde est deja suivie
    }
    snprintf(g_track_id, sizeof g_track_id, "%s", id);
    g_ntrack = 0;
  }
  last = now;
  if (g_ntrack == TRACK_MAX)
  {
    int i;                               // on garde un point sur deux
    for (i = 0; i < TRACK_MAX / 2; i++) g_track[i] = g_track[i * 2];
    g_ntrack = TRACK_MAX / 2;
  }
  g_track[g_ntrack].lat = lat;
  g_track[g_ntrack].lon = lon;
  g_track[g_ntrack].alt = alt;
  g_track[g_ntrack].t = now;
  g_ntrack++;
}

// Texte en continu (RTTY, CW) : lignes coupees a la largeur de la zone
static void stream_newline(void)
{
  if (g_stream_lines == 60)
  {
    memmove(g_stream[0], g_stream[1], sizeof g_stream[0] * 59);
    g_stream_lines--;
  }
  g_stream[g_stream_lines][0] = 0;
  g_stream_lines++;
  g_stream_col = 0;
  stream_px = 0;
}

void stream_put(const char *s, int n)
{
  int i;
  for (i = 0; i < n; i++)
  {
    unsigned char c = s[i];
    char one[2];
    if ((c >= 128) || (c == 127)) c = '?';             // hors des polices de la GUI
    one[0] = (char)c;
    one[1] = 0;
    if (c == '\n')
    {
      stream_newline();
      continue;
    }
    if ((c < 32) || (c == 127))
    {
      continue;
    }
    if ((g_stream_col >= 78) || (stream_px + text_width(F_R20, one) > 625))
    {
      stream_newline();
    }
    g_stream[g_stream_lines - 1][g_stream_col++] = c;
    g_stream[g_stream_lines - 1][g_stream_col] = 0;
    stream_px += text_width(F_R20, one);
  }
  g_dirty = true;
}

void stream_clear(void)
{
  g_stream_lines = 1;
  g_stream_col = 0;
  g_stream[0][0] = 0;
  stream_px = 0;
}

void data_clear(void)
{
  g_nmsg = g_msghead = 0;
  g_nobj = 0;
  g_ntrack = 0;
  g_track_id[0] = 0;
  g_status[0] = 0;
  memset(&g_radio, 0, sizeof g_radio);
  stream_clear();
  g_dirty = true;
}

// ---------------------------------------------------------------- geographie

void dist_az(double lat, double lon, double *km, double *az)
{
  double p1 = g_slat * M_PI / 180, p2 = lat * M_PI / 180;
  double dl = (lon - g_slon) * M_PI / 180;
  double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  *km = 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
  *az = fmod(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) * 180 / M_PI + 360, 360);
}

// Centre du carre d'un locator Maidenhead (4, 6, 8 ou 10 caracteres)
bool locator_to_ll(const char *loc, double *lat, double *lon)
{
  int n = strlen(loc);
  double lo, la, wlo = 20, wla = 10;
  if ((n < 4) || (n % 2))
  {
    return false;
  }
  if (toupper(loc[0]) < 'A' || toupper(loc[0]) > 'R' || toupper(loc[1]) < 'A' || toupper(loc[1]) > 'R'
    || !isdigit((unsigned char)loc[2]) || !isdigit((unsigned char)loc[3]))
  {
    return false;
  }
  lo = (toupper(loc[0]) - 'A') * 20 - 180;
  la = (toupper(loc[1]) - 'A') * 10 - 90;
  lo += (loc[2] - '0') * 2;
  la += (loc[3] - '0');
  wlo = 2; wla = 1;
  if (n >= 6 && isalpha((unsigned char)loc[4]) && isalpha((unsigned char)loc[5]))
  {
    lo += (toupper(loc[4]) - 'A') * wlo / 24;
    la += (toupper(loc[5]) - 'A') * wla / 24;
    wlo /= 24; wla /= 24;
    if (n >= 8 && isdigit((unsigned char)loc[6]) && isdigit((unsigned char)loc[7]))
    {
      lo += (loc[6] - '0') * wlo / 10;
      la += (loc[7] - '0') * wla / 10;
      wlo /= 10; wla /= 10;
      if (n >= 10 && isalpha((unsigned char)loc[8]) && isalpha((unsigned char)loc[9]))
      {
        lo += (toupper(loc[8]) - 'A') * wlo / 24;
        la += (toupper(loc[9]) - 'A') * wla / 24;
        wlo /= 24; wla /= 24;
      }
    }
  }
  *lon = lo + wlo / 2;
  *lat = la + wla / 2;
  return true;
}

// ---------------------------------------------------------------- journal

void log_line(const md_decoder *d, const char *head, const char *body)
{
  char path[160], ts[32];
  time_t now = time(NULL);
  struct tm tm;
  FILE *f;
  if (!strcmp(conf_get("log", "on"), "off"))
  {
    return;
  }
  localtime_r(&now, &tm);
  mkdir(MD_LOGDIR, 0755);
  snprintf(path, sizeof path, "%s/%s_%04d%02d%02d.log", MD_LOGDIR, d->key, tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday);
  strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
  f = fopen(path, "a");
  if (f)
  {
    fprintf(f, "%s | %s | %s\n", ts, head, body);
    fclose(f);
  }
}
