#define _GNU_SOURCE
// md_dec.c : les decodeurs du decodeur multiple (table, options) et la lecture
// de ce que chaque outil externe ecrit (texte, CSV ou JSON). Les commandes
// elles-memes sont dans run_decoder.sh.

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "md.h"

#define NOOPT {NULL, NULL, {NULL}, {NULL}, NULL}

const md_decoder DECODERS[] = {
  {"pocsag", "POCSAG", "POCSAG", "439.9875", false, V_LIST, P_MULTIMON, O_NONE, true, true,
   {{"rate", "Rate", {"all", "512", "1200", "2400"}, {"All"}, "all"},
    {"charset", "Charset", {"US", "FR", "DE", "SE", "SI"}, {NULL}, "US"},
    {"type", "Type", {"auto", "alpha", "numeric"}, {"Auto", "Alpha", "Numeric"}, "auto"},
    {"audio", "Audio", {"off", "on"}, {"OFF", "ON"}, "off"}}, {{NULL}}},
  {"flex", "FLEX", "FLEX", "169.6500", false, V_LIST, P_MULTIMON, O_NONE, true, true,
   {{"variant", "Decoder", {"FLEX_NEXT", "FLEX"}, {"Next", "Classic"}, "FLEX_NEXT"},
    {"audio", "Audio", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT}, {{NULL}}},
  {"aprs", "APRS", "APRS", "144.800", false, V_OBJ, P_APRS, O_APRS, true, true,
   {{"baud", "Baud", {"1200", "9600"}, {NULL}, "1200"}, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"rtty", "RTTY", "RTTY", "434.250", false, V_STREAM, P_MINIMODEM, O_NONE, true, false,
   {{"baud", "Baud", {"45.45", "50", "75", "100", "300"}, {NULL}, "50"},
    {"shift", "Shift", {"170", "425", "450", "600", "850"}, {NULL}, "425"},
    {"code", "Code", {"baudot", "ascii7", "ascii8"}, {"Baudot", "ASCII 7", "ASCII 8"}, "ascii7"},
    {"demod", "Demod", {"usb", "lsb"}, {"USB", "LSB"}, "usb"}},
   {{"Preset^UKHAS 434", "baud=50,shift=425,code=ascii7,demod=usb"},
    {"Preset^HF 45.45", "baud=45.45,shift=170,code=baudot,demod=lsb"}}},
  {"sonde", "Radio-^sonde", "Radiosonde", "403.000", false, V_SONDE, P_SONDE, O_SONDE, true, false,
   {{"types", "Types", {"all", "rs41", "m10m20", "dfm", "other"}, {"All", "RS41", "M10/M20", "DFM", "Others"}, "all"},
    {"audio", "Audio", {"on", "off"}, {"ON", "OFF"}, "on"},
    {"squelch", "Squelch", {"on", "off"}, {"ON", "OFF (hiss)"}, "on"}, NOOPT}, {{NULL}}},
  {"adsb", "ADS-B", "ADS-B", "1090", true, V_OBJ, P_ADSB, O_AIRCRAFT, false, false,
   {{"net", "Net ports", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"ism", "ISM^433/868", "ISM", "433.920", false, V_SENSOR, P_RTL433, O_SENSOR, false, false,
   {{"freq", "Band", {"433.920", "868.300", "868.950", "315.000"}, {"433.92", "868.30", "868.95", "315.00"}, "433.920"},
    {"hop", "Hop", {"off", "on"}, {"OFF", "433+868"}, "off"}, NOOPT, NOOPT}, {{NULL}}},
  {"ais", "AIS", "AIS", "162.000", true, V_OBJ, P_AIS, O_SHIP, false, false,
   {NOOPT, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"acars", "ACARS", "ACARS", "131.725", true, V_LIST, P_ACARS, O_NONE, false, false,
   {{"chans", "Channels", {"eu", "eu2"}, {"Europe", "Europe+"}, "eu"}, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"vdl2", "VDL2", "VDL2", "136.875", true, V_OBJ, P_VDL2, O_AIRCRAFT, false, false,
   {NOOPT, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"selcall", "Selcall^DTMF", "Selcall/DTMF", "145.500", false, V_LIST, P_SELCALL, O_NONE, true, true,
   {{"modes", "Tones", {"zvei1", "zvei2", "zvei3", "ccir", "eea"}, {"DTMF+ZVEI1", "DTMF+ZVEI2", "DTMF+ZVEI3", "DTMF+CCIR", "DTMF+EEA"}, "zvei1"},
    {"audio", "Audio", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT}, {{NULL}}},
  {"cw", "CW", "CW", "144.050", false, V_STREAM, P_CW, O_NONE, true, false,
   {{"tone", "Pitch", {"500", "600", "700", "800"}, {"500 Hz", "600 Hz", "700 Hz", "800 Hz"}, "700"},
    {"audio", "Audio", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT}, {{NULL}}},
  // deux rangees de modes sur la meme cle : 5 touches par rangee
  {"dmr", "Digital^Voice", "Digital Voice", "439.400", false, V_LIST, P_DSD, O_NONE, false, false,
   {{"mode", "Mode", {"auto", "dmr", "dpmr", "nxdn48", "dstar"}, {"Auto", "DMR", "dPMR", "NXDN48", "D-STAR"}, "auto"},
    {"mode", "Mode", {"ysf", "m17", "nxdn96", "p25"}, {"YSF C4FM", "M17", "NXDN96", "P25"}, "auto"},
    {"audio", "Audio", {"on", "off"}, {"ON", "OFF"}, "on"}, NOOPT}, {{NULL}}},
  {"ft8", "FT8", "FT8", "14.074", false, V_OBJ, P_FT8, O_STATION, true, false,
   {{"freq", "Band", {"3.573", "7.074", "10.136", "14.074", "28.074"}, {"80 m", "40 m", "30 m", "20 m", "10 m"}, "14.074"},
    {"mode", "Mode", {"ft8", "ft4"}, {"FT8", "FT4"}, "ft8"}, NOOPT, NOOPT}, {{NULL}}},
  {"wspr", "WSPR", "WSPR", "14.0956", false, V_OBJ, P_WSPR, O_STATION, false, false,
   {{"freq", "Band", {"3.5686", "7.0386", "10.1387", "14.0956", "28.1246"}, {"80 m", "40 m", "30 m", "20 m", "10 m"}, "14.0956"},
    {"upload", "Upload", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT}, {{NULL}}},
  {"rds", "FM^RDS", "FM RDS", "106.700", false, V_RADIO, P_RDS, O_SERVICE, true, true,
   {{"audio", "Audio", {"off", "on"}, {"OFF", "ON"}, "off"}, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  {"dab", "DAB+", "DAB+", "201.072", false, V_RADIO, P_DAB, O_SERVICE, false, false,
   {NOOPT, NOOPT, NOOPT, NOOPT}, {{NULL}}},
  // telecommandes (portails, garages, volets, cles de voiture, sonnettes) : les
  // protocoles de rtl_433 et, en decodeurs "flex", les codes fixes du Flipper
  // Zero ; meme tableau que les capteurs ISM
  {"remotes", "Remotes", "Remotes", "433.920", false, V_SENSOR, P_RTL433, O_SENSOR, false, false,
   {{"freq", "Band", {"433.920", "868.350", "315.000", "390.000"}, {"433.92", "868.35", "315.00", "390.00"}, "433.920"},
    {"set", "Set", {"remotes", "alarm"}, {"Remotes", "+ alarm sensors"}, "remotes"}, NOOPT, NOOPT}, {{NULL}}},
};
const int NDECODERS = sizeof DECODERS / sizeof DECODERS[0];

long g_adsb_msgs;
float g_adsb_rate;

const md_decoder *dec_find(const char *key)
{
  int i;
  for (i = 0; i < NDECODERS; i++)
  {
    if (!strcmp(DECODERS[i].key, key))
    {
      return &DECODERS[i];
    }
  }
  return &DECODERS[0];
}

static const char *ckey(const md_decoder *d, const char *k)
{
  static char s[64];
  snprintf(s, sizeof s, "%s_%s", d->key, k);
  return s;
}

const char *dec_opt(const md_decoder *d, int i)
{
  if (!strcmp(d->opt[i].key, "freq"))
  {
    return dec_freq(d);
  }
  return conf_get(ckey(d, d->opt[i].key), d->opt[i].def);
}

// Frequence enregistree, si c'est bien un nombre en MHz (sinon : par defaut)
const char *dec_freq(const md_decoder *d)
{
  const char *v;
  if (d->fixed)
  {
    return d->freq;
  }
  v = conf_get(ckey(d, "freq"), d->freq);
  if ((strspn(v, "0123456789.") != strlen(v)) || (atof(v) < 0.1) || (atof(v) > 2000))
  {
    return d->freq;
  }
  return v;
}

const char *dec_gain(const md_decoder *d)
{
  return conf_get(ckey(d, "gain"), "auto");
}

// ---------------------------------------------------------------- outils

static void trim(char *s)
{
  int n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
  memmove(s, s + strspn(s, " \t"), strlen(s + strspn(s, " \t")) + 1);
}

// Retire les "<NUL>" (caracteres de bourrage affiches par multimon-ng)
// et les espaces de fin
static void strip_tags(char *s)
{
  char *p;
  while ((p = strstr(s, "<NUL>")))
  {
    memmove(p, p + 5, strlen(p + 5) + 1);
  }
  trim(s);
}

static void clean(char *s)          // caracteres non imprimables remplaces
{
  for (; *s; s++)
  {
    if ((unsigned char)*s < 32) *s = ' ';
  }
}

static double num(const char *js, const char *path)
{
  double v;
  return json_num(js, path, &v) ? v : NAN;
}

static void add(const md_decoder *d, md_rgb col, const char *head, const char *body)
{
  msg_add(col, head, body);
  log_line(d, head, body);
}

// Separe une ligne CSV (champs entre guillemets possibles)
static int csv_split(char *ln, char **f, int max)
{
  int n = 0;
  char *p = ln, *o;
  while (n < max)
  {
    f[n++] = o = p;
    if (*p == '"')
    {
      for (p++; *p; p++)
      {
        if (*p == '"' && p[1] == '"') { *o++ = '"'; p++; }
        else if (*p == '"') { p++; break; }
        else *o++ = *p;
      }
    }
    while (*p && (*p != ',')) *o++ = *p++;
    if (!*p) { *o = 0; break; }
    *o = 0;
    p++;
  }
  return n;
}

// Locator Maidenhead a 4 caracteres (et pas RR73)
static bool is_grid(const char *s)
{
  return (strlen(s) == 4) && (s[0] >= 'A') && (s[0] <= 'R') && (s[1] >= 'A') && (s[1] <= 'R')
    && isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) && strcmp(s, "RR73");
}

static bool is_call(const char *s)
{
  int n = strlen(s), digits = 0, i;
  if ((n < 3) || (n > 12)) return false;
  for (i = 0; i < n; i++)
  {
    if (isdigit((unsigned char)s[i])) digits++;
    else if (!isupper((unsigned char)s[i]) && (s[i] != '/')) return false;
  }
  return digits > 0 && digits < n;
}

// ---------------------------------------------------------------- multimon-ng

static char dtmf_seq[64];
static time_t dtmf_last;

// Message POCSAG "numerique" (fonction 0) qui est en realite du texte :
// certains reseaux (SDIS sur 173,7 MHz, e*Message...) envoient l'alpha-
// numerique avec la fonction 0, et multimon-ng le rend en chiffres
// ("147.3U.  02539[47..."). Ces chiffres sont les quartets du message tels
// quels (table de multimon-ng) : on les reassemble en mots de 7 bits (poids
// faible en tete) et on garde le texte s'il est nettement plus vraisemblable,
// avec les notes de multimon-ng (numerique : U -10, [ ] -5, espace . - -2 ;
// texte : lettres +1, ponctuation -2, caracteres de commande -5).
static bool numeric_to_alpha(const char *num, char *out, int size)
{
  static const char *conv = "084 2.6]195-3U7[";
  unsigned acc = 0;
  int nbits = 0, i, k = 0, sn = 0, sa = 0, zeros = 0;
  for (i = 0; num[i]; i++)
  {
    const char *q = strchr(conv, num[i]);
    if (!q) return false;
    acc = (acc << 4) | (unsigned)(q - conv);
    nbits += 4;
    sn += (num[i] == 'U') ? -10 : ((num[i] == '[') || (num[i] == ']')) ? -5 :
          ((num[i] == ' ') || (num[i] == '.') || (num[i] == '-')) ? -2 : (i < 10 ? 5 : 0);
    while (nbits >= 7)
    {
      unsigned v = (acc >> (nbits - 7)) & 0x7f, c = 0;
      int b;
      nbits -= 7;
      for (b = 0; b < 7; b++) if (v & (1u << b)) c |= 1u << (6 - b);
      if (c == 0) { zeros++; continue; }       // bourrage en fin de message
      sa += ((c < 32) || (c == 127)) ? -5 :
            (((c > 32) && (c < 48)) || ((c > 57) && (c < 65)) || ((c > 90) && (c < 97)) || ((c > 122) && (c < 127))) ? -2 : 1;
      if (k < size - 1) out[k++] = ((c >= 32) && (c < 127)) ? (char)c : ' ';
    }
  }
  out[k] = 0;
  trim(out);
  return (k >= 4) && (sa >= 20) && (sa > sn) && (zeros <= 2);
}

static void parse_multimon(const md_decoder *d, char *ln)
{
  char head[100], body[600], name[32] = "", v[600];
  if (ln[0] == '{')
  {
    json_str(ln, "demod_name", name, sizeof name);
    if (!strcmp(name, "DTMF"))
    {
      if (json_str(ln, "digit", v, sizeof v) && (strlen(dtmf_seq) < sizeof dtmf_seq - 2))
      {
        strcat(dtmf_seq, v);
        dtmf_last = time(NULL);
        snprintf(g_status, sizeof g_status, "DTMF %s", dtmf_seq);
        g_dirty = true;
      }
      return;
    }
    if (!strncmp(name, "POCSAG", 6))
    {
      double a = num(ln, "address"), f = num(ln, "function");
      const char *type = "Tone";
      body[0] = 0;
      if (json_str(ln, "alpha", body, sizeof body)) type = "Alpha";
      else if (json_str(ln, "numeric", body, sizeof body))
      {
        char txt[600];
        type = "Numeric";
        if (strcmp(conf_get(ckey(d, "type"), "auto"), "numeric") && numeric_to_alpha(body, txt, sizeof txt))
        {
          snprintf(body, sizeof body, "%s", txt);
          type = "Alpha (F0)";                 // texte envoye avec la fonction 0
        }
      }
      strip_tags(body);
      if (!body[0]) snprintf(body, sizeof body, "(tone only)");
      clean(body);
      {
        double bad = num(ln, "bad");       // md_pocsag : mots que le BCH n'a pas pu corriger
        char warn[32] = "";
        if (isfinite(bad) && (bad > 0)) snprintf(warn, sizeof warn, "   %.0f bad word%s", bad, bad > 1 ? "s" : "");
        snprintf(head, sizeof head, "%s   RIC %07.0f   F%.0f   %s%s", name, isfinite(a) ? a : 0,
                 isfinite(f) ? f : 0, type, warn);
      }
      add(d, C_WHITE, head, body);
      return;
    }
    // FLEX (classique ou FLEX_NEXT)
    {
      double cap = num(ln, "capcode"), baud = num(ln, "baud");
      char mt[24] = "";
      if (!isfinite(baud)) baud = num(ln, "sync_baud");
      if (!json_str(ln, "type_tag", mt, sizeof mt) && !json_str(ln, "msg_type", mt, sizeof mt))
      {
        snprintf(mt, sizeof mt, "%s", name);
      }
      body[0] = 0;
      if (!json_str(ln, "message", body, sizeof body))
      {
        if (!json_str(ln, "alpha", body, sizeof body)) json_str(ln, "numeric", body, sizeof body);
      }
      if (!isfinite(cap) && !body[0]) return;
      strip_tags(body);
      clean(body);
      snprintf(head, sizeof head, "FLEX %.0f   Cap %09.0f   %s", isfinite(baud) ? baud : 0,
               isfinite(cap) ? cap : 0, mt);
      add(d, C_WHITE, head, body[0] ? body : "(no text)");
    }
    return;
  }
  // Signalisation sequentielle (ZVEI, CCIR, EEA, EIA...) : "ZVEI1: 12345"
  {
    char *c = strchr(ln, ':');
    if (c && (c - ln < 12) && strncmp(ln, "Enabled", 7) && strncmp(ln, "multimon", 8))
    {
      snprintf(name, sizeof name, "%.*s", (int)(c - ln), ln);
      snprintf(body, sizeof body, "%s", c + 1);
      trim(body);
      if (!strcmp(name, "DTMF"))
      {
        strncat(dtmf_seq, body, sizeof dtmf_seq - strlen(dtmf_seq) - 1);
        dtmf_last = time(NULL);
        return;
      }
      if (body[0])
      {
        add(d, C_WHITE, name, body);
      }
    }
  }
}

// ---------------------------------------------------------------- APRS (direwolf -L)

static void parse_aprs(const md_decoder *d, char *ln)
{
  char *f[24], head[100], body[600], tmp[200];
  int n;
  md_obj *o;
  double lat, lon;
  if (!strncmp(ln, "chan,", 5))
  {
    return;                              // en-tete du journal
  }
  n = csv_split(ln, f, 24);
  if (n < 22)
  {
    return;
  }
  // 3 source, 4 heard, 5 level, 8 name, 9 symbol, 10 lat, 11 lon, 12 speed (kt),
  // 13 course, 14 alt (m), 18 system, 19 status, 21 comment
  snprintf(head, sizeof head, "%s%s%s   level %s", f[3], strcmp(f[3], f[4]) && f[4][0] ? "  via " : "",
           strcmp(f[3], f[4]) ? f[4] : "", f[5]);
  body[0] = 0;
  lat = f[10][0] ? atof(f[10]) : NAN;
  lon = f[11][0] ? atof(f[11]) : NAN;
  if (isfinite(lat) && isfinite(lon))
  {
    snprintf(tmp, sizeof tmp, "%.4f %.4f  ", lat, lon);
    strcat(body, tmp);
  }
  if (f[12][0]) { snprintf(tmp, sizeof tmp, "%.0f km/h  ", atof(f[12]) * 1.852); strcat(body, tmp); }
  if (f[13][0]) { snprintf(tmp, sizeof tmp, "%s deg  ", f[13]); strcat(body, tmp); }
  if (f[14][0]) { snprintf(tmp, sizeof tmp, "%.0f m  ", atof(f[14])); strcat(body, tmp); }
  if (f[8][0] && strcmp(f[8], f[3])) { snprintf(tmp, sizeof tmp, "[%s]  ", f[8]); strcat(body, tmp); }
  if (strcasestr(f[18], "unknown")) f[18][0] = 0;          // "Unknown Unknown", "UNKNOWN vendor/model"
  snprintf(tmp, sizeof tmp, "%s %s %s", f[19], f[21], f[18]);
  trim(tmp);
  strncat(body, tmp, sizeof body - strlen(body) - 1);
  clean(body);
  add(d, C_WHITE, head, body[0] ? body : "(no position)");

  o = obj_get(O_APRS, f[8][0] ? f[8] : f[3]);
  snprintf(o->sym, sizeof o->sym, "%s", f[9]);             // symbole : icone du radar
  snprintf(o->info, sizeof o->info, "%s", f[21][0] ? f[21] : f[19]);
  clean(o->info);
  if (f[12][0]) o->spd = atof(f[12]);
  if (f[13][0]) o->crs = atof(f[13]);
  if (f[14][0]) o->alt = atof(f[14]);
  obj_pos(o, lat, lon);
}

// ---------------------------------------------------------------- radiosondes (rs1729, JSON)

static void parse_sonde(const md_decoder *d, char *ln)
{
  char id[24], type[24] = "", sub[24] = "", head[100], body[600];
  double lat, lon, alt, v;
  md_obj *o;
  time_t now = time(NULL);
  if ((ln[0] != '{') || !json_str(ln, "id", id, sizeof id))
  {
    return;
  }
  json_str(ln, "type", type, sizeof type);
  // sous-type : "RS41-SGP", "M20"... ; celui des M10 est un code ("0x9F") : le type a la place
  if (!json_str(ln, "subtype", sub, sizeof sub) || !strncmp(sub, "0x", 2) || !sub[0])
  {
    snprintf(sub, sizeof sub, "%s", type);
  }
  // largeur du filtre de l'ecoute (md_fmdemod) selon le type decode, en kHz
  {
    static const struct { const char *t; int khz; } BW[] = {
      {"RS41", 10}, {"RS92", 10}, {"M10", 24}, {"M20", 24}, {"DFM", 12}, {"LMS", 16}, {"MEISEI", 16},
      {"IMET", 10}, {"MP3", 12}, {"MTS", 8}};
    static int last = -1;
    struct stat st;
    int k, khz = 16;
    for (k = 0; k < (int)(sizeof BW / sizeof BW[0]); k++)
    {
      if (!strncasecmp(type, BW[k].t, strlen(BW[k].t))) { khz = BW[k].khz; break; }
    }
    if (type[0] && ((khz != last) || stat(MD_SHM "/sonde_bw", &st)))   // efface au lancement du decodeur
    {
      FILE *f = fopen(MD_SHM "/sonde_bw", "w");
      if (f)
      {
        fprintf(f, "%d\n", khz);
        fclose(f);
        last = khz;
      }
    }
  }
  o = obj_get(O_SONDE, id);
  snprintf(o->name, sizeof o->name, "%s", sub);
  lat = num(ln, "lat");
  lon = num(ln, "lon");
  alt = num(ln, "alt");
  obj_pos(o, lat, lon);
  if (isfinite(alt)) o->alt = alt;
  if (isfinite(v = num(ln, "vel_h"))) o->spd = v * 1.943844;
  if (isfinite(v = num(ln, "heading"))) o->crs = v;
  if (isfinite(v = num(ln, "vel_v"))) o->vz = v;
  if (isfinite(v = num(ln, "temp"))) o->temp = v;
  if (isfinite(v = num(ln, "humidity"))) o->hum = v;
  // M10 : humidite donnee par m10m20mod en -vv seulement, formule empirique sans
  // etalonnage (rs1729) : affichee "~" (approchee)
  o->hum_approx = !strncasecmp(type, "M10", 3);
  if (isfinite(v = num(ln, "batt"))) o->batt = v;
  if (isfinite(v = num(ln, "frame"))) o->frame = (int)v;
  if (isfinite(v = num(ln, "bt"))) o->burst = (v >= 0 && v < 65535) ? (int)v : -1;
  if (isfinite(v = num(ln, "sats"))) snprintf(o->info, sizeof o->info, "%.0f sats", v);
  if (o->pos && isfinite(alt))
  {
    track_add(id, o->lat, o->lon, alt);
  }
  g_dirty = true;
  // Un message (et une ligne de journal) a la premiere trame puis toutes les 30 s
  if (now - o->lastmsg >= 30)
  {
    o->lastmsg = now;
    snprintf(head, sizeof head, "%s   %s   frame %d", sub, id, o->frame);
    char rh[16] = "-";
    if (isfinite(o->hum)) snprintf(rh, sizeof rh, "%s%.0f %%", o->hum_approx ? "~" : "", o->hum);
    snprintf(body, sizeof body, "%.5f %.5f   alt %.0f m   vz %+.1f m/s   T %.1f C   RH %s   batt %.1f V",
             o->lat, o->lon, o->alt, o->vz, o->temp, rh, o->batt);
    add(d, C_WHITE, head, body);
  }
}

// ---------------------------------------------------------------- ADS-B (readsb, aircraft.json)

static void poll_adsb(const md_decoder *d)
{
  static char *buf;
  static time_t last;
  static long prev_msgs;
  static time_t prev_t;
  time_t now = time(NULL);
  const char *p;
  FILE *f;
  long n;
  if (now == last)
  {
    return;
  }
  last = now;
  f = fopen(MD_SHM "/adsb/aircraft.json", "r");
  if (!f)
  {
    return;
  }
  if (!buf) buf = malloc(1 << 20);
  n = fread(buf, 1, (1 << 20) - 1, f);
  fclose(f);
  buf[n] = 0;
  {
    double m = num(buf, "messages");
    if (isfinite(m))
    {
      g_adsb_msgs = (long)m;
      if (prev_t && (now - prev_t >= 10))
      {
        g_adsb_rate = (g_adsb_msgs - prev_msgs) * 60.0f / (now - prev_t);
        prev_msgs = g_adsb_msgs;
        prev_t = now;
      }
      if (!prev_t) { prev_msgs = g_adsb_msgs; prev_t = now; }
    }
  }
  p = json_find(buf, "aircraft", NULL);
  if (!p || (*p != '['))
  {
    return;
  }
  for (p++;;)
  {
    const char *e;
    char hex[16], flight[16] = "";
    double seen, v;
    while (*p && (isspace((unsigned char)*p) || (*p == ','))) p++;
    if (*p != '{') break;
    e = json_skip(p);
    seen = num(p, "seen");
    if (json_str(p, "hex", hex, sizeof hex) && (!isfinite(seen) || (seen < 60)))
    {
      md_obj *o;
      bool isnew;
      int i;
      for (i = 0; hex[i]; i++) hex[i] = toupper((unsigned char)hex[i]);
      o = obj_get(O_AIRCRAFT, hex);
      isnew = (o->count == 1);
      o->count = isnew ? 1 : o->count - 1;          // compteur : avions, pas lectures
      o->seen = now - (time_t)(isfinite(seen) ? seen : 0);
      if (json_str(p, "flight", flight, sizeof flight))
      {
        trim(flight);
        if (flight[0] && strcmp(flight, o->name))  // indicatif recu (ou change) : nouveau message
        {
          snprintf(o->name, sizeof o->name, "%s", flight);
          isnew = true;
        }
      }
      if (isfinite(v = num(p, "alt_baro"))) o->alt = v * 0.3048;
      if (isfinite(v = num(p, "gs"))) o->spd = v;
      if (isfinite(v = num(p, "track"))) o->crs = v;
      if (isfinite(v = num(p, "rssi"))) o->snr = v;
      if (isfinite(v = num(p, "baro_rate"))) o->vz = v * 0.00508;
      obj_pos(o, num(p, "lat"), num(p, "lon"));
      // vue List : un message a l'apparition de l'avion, un autre a l'arrivee
      // de son indicatif (comme l'AIS) ; aircraft.json n'a pas de messages
      if (isnew)
      {
        char head[100], body[200], one[40];
        snprintf(head, sizeof head, "ICAO %s", hex);
        snprintf(body, sizeof body, "%s", o->name[0] ? o->name : "(callsign not yet received)");
        if (isfinite(o->alt))
        {
          snprintf(one, sizeof one, "  %.0f ft", o->alt / 0.3048);
          strcat(body, one);
        }
        if (isfinite(o->spd)) { snprintf(one, sizeof one, "  %.0f kt", o->spd); strcat(body, one); }
        if (isfinite(o->crs)) { snprintf(one, sizeof one, "  hdg %03.0f", o->crs); strcat(body, one); }
        if (o->pos && isfinite(g_slat))
        {
          double km, az;
          dist_az(o->lat, o->lon, &km, &az);
          snprintf(one, sizeof one, "  %.0f km", km);
          strcat(body, one);
        }
        add(d, C_WHITE, head, body);
      }
      g_dirty = true;
    }
    p = e;
  }
}

// ---------------------------------------------------------------- capteurs (rtl_433, JSON)

static void parse_rtl433(const md_decoder *d, char *ln)
{
  static const char *skip[] = {"time", "model", "id", "channel", "mod", "freq", "freq1", "freq2", "rssi",
                               "snr", "noise", "mic", "protocol", "subtype", "codes", "rows", "count", "num_rows", NULL};
  char model[48] = "", id[24] = "", ch[12] = "", key[160], head[100], body[600] = "", info[96] = "", bits[16] = "";
  const char *p;
  md_obj *o;
  double v;
  if (ln[0] != '{' || !json_str(ln, "model", model, sizeof model))
  {
    return;
  }
  json_str(ln, "id", id, sizeof id);
  if (!id[0])                                  // decodeur flex (telecommande a code fixe) : le code est l'identite
  {
    const char *c = json_find(ln, "codes", NULL);   // "codes":["{24}1a2b3c", ...]
    if (c && (*c == '['))
    {
      const char *q = strchr(c, '"');
      const char *b = q ? strchr(q, '}') : NULL, *e = q ? strchr(q + 1, '"') : NULL;
      if (q && b && e && (q[1] == '{') && (b < e))
      {
        snprintf(bits, sizeof bits, "%.*s bits", (int)(b - q - 2), q + 2);
        snprintf(id, sizeof id, "%.*s", (int)(e - b - 1), b + 1);
      }
    }
  }
  json_str(ln, "channel", ch, sizeof ch);
  snprintf(key, sizeof key, "%s %s%s%s", model, id, ch[0] ? "/" : "", ch);
  o = obj_get(O_SENSOR, key);
  snprintf(o->name, sizeof o->name, "%s", model);
  snprintf(o->extra, sizeof o->extra, "%s%s%s", id, ch[0] ? " ch " : "", ch);
  if (isfinite(v = num(ln, "temperature_C"))) o->temp = v;
  else if (isfinite(v = num(ln, "temperature_F"))) o->temp = (v - 32) / 1.8;
  if (isfinite(v = num(ln, "humidity"))) o->hum = v;
  if (isfinite(v = num(ln, "battery_ok"))) o->batt = v;
  if (isfinite(v = num(ln, "rssi"))) o->snr = v;
  // toutes les valeurs, sauf l'identification
  p = ln + 1;
  while (*(p += strspn(p, " \t,")) == '"')
  {
    char k[40], val[64];
    const char *ke = strchr(p + 1, '"'), *vs, *ve;
    int i, skipit = 0;
    if (!ke) break;
    snprintf(k, sizeof k, "%.*s", (int)(ke - p - 1), p + 1);
    vs = ke + 1 + strspn(ke + 1, " :");
    ve = json_skip(vs);
    snprintf(val, sizeof val, "%.*s", (int)(ve - vs), vs);
    for (i = 0; skip[i]; i++) if (!strcmp(k, skip[i])) skipit = 1;
    if (!skipit)
    {
      char one[110];
      if (val[0] == '"') { memmove(val, val + 1, strlen(val)); val[strcspn(val, "\"")] = 0; }
      snprintf(one, sizeof one, "%s=%s  ", k, val);
      strncat(body, one, sizeof body - strlen(body) - 1);
      if (strlen(info) + strlen(one) < sizeof info) strcat(info, one);
    }
    p = ve;
  }
  // resume en clair d'abord (temperature, humidite, pile), puis le reste
  {
    char sum[96] = "", one[40];
    if (bits[0]) { snprintf(one, sizeof one, "%s  ", bits); strcat(sum, one); }
    if (isfinite(o->temp)) { snprintf(one, sizeof one, "%.1f C  ", o->temp); strcat(sum, one); }
    if (isfinite(o->hum)) { snprintf(one, sizeof one, "%.0f %%  ", o->hum); strcat(sum, one); }
    if (isfinite(o->batt)) strcat(sum, o->batt > 0 ? "batt OK  " : "batt LOW  ");
    {
      char *p2 = info, *q;
      static const char *dup[] = {"temperature_C=", "temperature_F=", "humidity=", "battery_ok=", NULL};
      int k;
      for (k = 0; dup[k]; k++)
      {
        while ((q = strstr(p2, dup[k])))
        {
          char *e2 = strstr(q, "  ");
          memmove(q, e2 ? e2 + 2 : q + strlen(q), strlen(e2 ? e2 + 2 : q + strlen(q)) + 1);
        }
      }
    }
    snprintf(o->info, sizeof o->info, "%s%s", sum, info);
  }
  snprintf(head, sizeof head, "%s   id %s%s%s   %.1f dB", model, id, ch[0] ? "  ch " : "", ch,
           isfinite(o->snr) ? o->snr : 0.0);
  add(d, C_WHITE, head, body[0] ? body : (bits[0] ? bits : "-"));
}

// ---------------------------------------------------------------- AIS (AIS-catcher, JSON)

static void parse_ais(const md_decoder *d, char *ln)
{
  char mmsi[16], name[32] = "", call[16] = "", dest[32] = "", head[100], body[300];
  md_obj *o;
  double v;
  bool isnew;
  if ((ln[0] != '{') || !json_str(ln, "mmsi", mmsi, sizeof mmsi))
  {
    return;
  }
  o = obj_get(O_SHIP, mmsi);
  isnew = (o->count == 1);
  if (json_str(ln, "shipname", name, sizeof name))
  {
    char *at = strchr(name, '@');
    if (at) *at = 0;
    trim(name);
    if (name[0] && strcmp(name, o->name))
    {
      snprintf(o->name, sizeof o->name, "%s", name);
      isnew = true;
    }
  }
  json_str(ln, "callsign", call, sizeof call);
  json_str(ln, "destination", dest, sizeof dest);
  if (call[0] || dest[0])
  {
    trim(call);
    trim(dest);
    snprintf(o->info, sizeof o->info, "%s %s", call, dest);
  }
  if (isfinite(v = num(ln, "speed")) && v < 102) o->spd = v;
  if (isfinite(v = num(ln, "course")) && v < 360) o->crs = v;
  if (isfinite(v = num(ln, "signalpower"))) o->snr = v;
  v = num(ln, "lat");
  if (isfinite(v) && fabs(v) <= 90) obj_pos(o, v, num(ln, "lon"));
  g_dirty = true;
  if (isnew)
  {
    snprintf(head, sizeof head, "MMSI %s", mmsi);
    snprintf(body, sizeof body, "%s  %s", o->name[0] ? o->name : "(name not yet received)", o->info);
    add(d, C_WHITE, head, body);
  }
}

// ---------------------------------------------------------------- ACARS (acarsdec, JSON)

static void parse_acars(const md_decoder *d, char *ln)
{
  char tail[16] = "", flight[16] = "", label[8] = "", text[400] = "", head[100];
  double fr, lvl;
  if (ln[0] != '{')
  {
    return;
  }
  json_str(ln, "tail", tail, sizeof tail);
  json_str(ln, "flight", flight, sizeof flight);
  json_str(ln, "label", label, sizeof label);
  json_str(ln, "text", text, sizeof text);
  fr = num(ln, "freq");
  lvl = num(ln, "level");
  clean(text);
  snprintf(head, sizeof head, "%.3f   %s   %s   label %s   %.0f dB", isfinite(fr) ? fr : 0, tail, flight,
           label, isfinite(lvl) ? lvl : 0);
  add(d, C_WHITE, head, text[0] ? text : "(no text)");
}

// ---------------------------------------------------------------- VDL2 (dumpvdl2, JSON)

static void parse_vdl2(const md_decoder *d, char *ln)
{
  char src[16] = "", stype[24] = "", dst[16] = "", dtype[24] = "", head[100], body[600] = "";
  char reg[16] = "", flight[16] = "", label[8] = "", text[400] = "", ft[8] = "";
  const char *loc;
  double fr, lvl;
  if (ln[0] != '{')
  {
    return;
  }
  json_str(ln, "vdl2.avlc.src.addr", src, sizeof src);
  json_str(ln, "vdl2.avlc.src.type", stype, sizeof stype);
  json_str(ln, "vdl2.avlc.dst.addr", dst, sizeof dst);
  json_str(ln, "vdl2.avlc.dst.type", dtype, sizeof dtype);
  json_str(ln, "vdl2.avlc.frame_type", ft, sizeof ft);
  fr = num(ln, "vdl2.freq");
  lvl = num(ln, "vdl2.sig_level");
  if (json_str(ln, "vdl2.avlc.acars.reg", reg, sizeof reg))
  {
    json_str(ln, "vdl2.avlc.acars.flight", flight, sizeof flight);
    json_str(ln, "vdl2.avlc.acars.label", label, sizeof label);
    json_str(ln, "vdl2.avlc.acars.msg_text", text, sizeof text);
    clean(text);
    snprintf(body, sizeof body, "ACARS %s %s %s: %s", reg, flight, label, text[0] ? text : "(no text)");
  }
  else if (strstr(ln, "\"xid\""))
  {
    snprintf(body, sizeof body, "XID (link management)");
  }
  else
  {
    snprintf(body, sizeof body, "%s frame", ft);
  }
  snprintf(head, sizeof head, "%.3f   %s %s  >  %s %s   %.0f dB", isfinite(fr) ? fr / 1e6 : 0, stype, src,
           dtype, dst, isfinite(lvl) ? lvl : 0);
  // Avion : position annoncee dans l'echange XID (ac_location)
  if (!strcmp(stype, "Aircraft") && src[0])
  {
    md_obj *o = obj_get(O_AIRCRAFT, src);
    if (flight[0]) { trim(flight); snprintf(o->name, sizeof o->name, "%s", flight); }
    else if (reg[0] && !o->name[0]) snprintf(o->name, sizeof o->name, "%s", reg + (reg[0] == '.'));
    if (isfinite(lvl)) o->snr = lvl;
    loc = strstr(ln, "\"ac_location\"");
    if (loc)
    {
      const char *la = strstr(loc, "\"lat\""), *lo = strstr(loc, "\"lon\""), *al = strstr(loc, "\"alt\"");
      if (la && lo)
      {
        obj_pos(o, atof(la + 6 + strspn(la + 6, " :")), atof(lo + 6 + strspn(lo + 6, " :")));
        strcat(body, "  (position)");
      }
      if (al) o->alt = atof(al + 6 + strspn(al + 6, " :")) * 0.3048;
    }
  }
  add(d, C_WHITE, head, body);
}

// ---------------------------------------------------------------- DMR, NXDN... (dsd-fme, texte)

static void parse_dsd(const md_decoder *d, char *ln)
{
  static char last[200];
  static time_t lastt;
  char s[600], *o = s, *p;
  time_t now = time(NULL);
  // sans les sequences de couleur ANSI
  for (p = ln; *p && (o - s < (int)sizeof s - 1); p++)
  {
    if (*p == 27)
    {
      while (*p && !isalpha((unsigned char)*p)) p++;
      if (!*p) break;
      continue;
    }
    *o++ = *p;
  }
  *o = 0;
  clean(s);
  trim(s);
  if (!s[0] || strstr(s, "No Sync") || strstr(s, "no sync"))
  {
    return;
  }
  // banniere et lignes de demarrage de dsd-fme : pas des messages
  {
    static const char *boot[] = {"Build Version", "MBElib", "CODEC2", "Decoding ", "RTL Input", "Selected Device",
      "Dev 0", "Audio In", "Audio Out", "Setting ", "Found ", "Using ", "Tuner ", "WARNING", "(Center", "Oversampling",
      "Buffer size", "Exact sample", "Sampling at", "Output at", "cleaning up", "0:  ", "UDP", NULL};
    int i;
    for (p = s; *p; p++) if ((unsigned char)*p >= 0x80) return;
    for (i = 0; boot[i]; i++) if (!strncmp(s, boot[i], strlen(boot[i]))) return;
  }
  if (!strncmp(s, last, sizeof last - 1) && (now - lastt < 3))
  {
    return;                              // meme ligne repetee
  }
  snprintf(last, sizeof last, "%s", s);
  lastt = now;
  add(d, C_WHITE, "dsd-fme", s);
}

// ---------------------------------------------------------------- FT8 (md_ft8 + decode_ft8)

// md_ft8 : "FT8 HHMMSS snr dt freq ~  message" (snr, dt, freq : decode_ft8)
static void parse_ft8(const md_decoder *d, char *ln)
{
  char slot[8], *tilde, msg[64], *tok[8], head[100], *save = NULL;
  int n = 0;
  float snr = 0, dt = 0, fq = 0;
  if (strncmp(ln, "FT", 2) || !(tilde = strchr(ln, '~')))
  {
    return;
  }
  if (sscanf(ln, "%*s %7s %f %f %f", slot, &snr, &dt, &fq) < 4)
  {
    return;
  }
  snprintf(msg, sizeof msg, "%s", tilde + 1);
  trim(msg);
  snprintf(head, sizeof head, "%.2s:%.2s:%.2s   %+.0f dB   dt %+.1f   %4.0f Hz", slot, slot + 2, slot + 4, snr, dt, fq);
  add(d, C_WHITE, head, msg);
  {
    char m2[64];
    snprintf(m2, sizeof m2, "%s", msg);
    for (char *t = strtok_r(m2, " ", &save); t && n < 8; t = strtok_r(NULL, " ", &save)) tok[n++] = t;
    // "CQ [DX] CALL GRID" ou "CALL1 CALL2 GRID" : la station qui emet est la derniere avant le locator
    if ((n >= 3) && is_grid(tok[n - 1]) && is_call(tok[n - 2]))
    {
      double lat, lon;
      md_obj *o = obj_get(O_STATION, tok[n - 2]);
      snprintf(o->name, sizeof o->name, "%s", tok[n - 1]);
      o->snr = snr;
      if (locator_to_ll(tok[n - 1], &lat, &lon)) obj_pos(o, lat, lon);
    }
  }
}

// "Spot(0)  22.80   0.01  14.097150  0    K1JT   FN20 20"
static void parse_wspr(const md_decoder *d, char *ln)
{
  char call[16], loc[8], head[100], body[100];
  float snr, dt;
  double fq;
  int drift, pwr;
  double lat, lon;
  md_obj *o;
  if (strncmp(ln, "Spot", 4) || (sscanf(ln, "%*s %f %f %lf %d %15s %7s %d", &snr, &dt, &fq, &drift, call, loc,
                                        &pwr) != 7))
  {
    return;
  }
  snprintf(head, sizeof head, "%.6f MHz   %+.0f dB   dt %+.1f   drift %d", fq, snr, dt, drift);
  snprintf(body, sizeof body, "%s  %s  %d dBm", call, loc, pwr);
  add(d, C_WHITE, head, body);
  o = obj_get(O_STATION, call);
  snprintf(o->name, sizeof o->name, "%s", loc);
  o->snr = snr;
  if (locator_to_ll(loc, &lat, &lon)) obj_pos(o, lat, lon);
}

// ---------------------------------------------------------------- radio (RDS, DAB+)

md_radio g_radio;

// Canaux DAB de la bande III (MHz)
static const struct { const char *ch; double mhz; } DAB_CH[] = {
  {"5A", 174.928}, {"5B", 176.640}, {"5C", 178.352}, {"5D", 180.064}, {"6A", 181.936}, {"6B", 183.648},
  {"6C", 185.360}, {"6D", 187.072}, {"7A", 188.928}, {"7B", 190.640}, {"7C", 192.352}, {"7D", 194.064},
  {"8A", 195.936}, {"8B", 197.648}, {"8C", 199.360}, {"8D", 201.072}, {"9A", 202.928}, {"9B", 204.640},
  {"9C", 206.352}, {"9D", 208.064}, {"10A", 209.936}, {"10N", 210.096}, {"10B", 211.648}, {"10C", 213.360},
  {"10D", 215.072}, {"11A", 216.928}, {"11N", 217.088}, {"11B", 218.640}, {"11C", 220.352}, {"11D", 222.064},
  {"12A", 223.936}, {"12N", 224.096}, {"12B", 225.648}, {"12C", 227.360}, {"12D", 229.072}, {"13A", 230.784},
  {"13B", 232.496}, {"13C", 234.208}, {"13D", 235.776}, {"13E", 237.488}, {"13F", 239.200}};

// Canal le plus proche de la frequence (MHz) ; jamais NULL (start_decoder le
// passe a run_decoder.sh pour tous les decodeurs)
const char *dab_channel(const char *freq)
{
  double f = atof(freq), best = 1e9;
  const char *ch = DAB_CH[0].ch;
  unsigned i;
  for (i = 0; i < sizeof DAB_CH / sizeof DAB_CH[0]; i++)
  {
    if (fabs(DAB_CH[i].mhz - f) < best)
    {
      best = fabs(DAB_CH[i].mhz - f);
      ch = DAB_CH[i].ch;
    }
  }
  return ch;
}

// Heure diffusee (debut de la minute utc) recue maintenant ; latency : delai
// estime de la chaine de reception
void radio_time(time_t utc, int lto, double latency)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  g_radio.utc = utc;
  g_radio.lto = lto;
  g_radio.got = tv.tv_sec;
  g_radio.diff = tv.tv_sec + tv.tv_usec / 1e6 - latency - (double)utc;
  g_dirty = true;
}

// Texte JSON vers ASCII affichable, espaces de debut et de fin retires
static void radio_txt(char *s)
{
  clean(s);
  ascii_fold(s);
  trim(s);
}

// redsea : une ligne JSON par groupe RDS (une dizaine par seconde) ; seuls
// les changements (nom, radiotexte) vont dans les messages et le journal
static void parse_rds(const md_decoder *d, char *ln)
{
  char pi[12] = "", ps[40] = "", rt[96] = "", pty[24] = "", ct[40] = "", m[200];
  const char *af;
  md_obj *o;
  double v;
  if ((ln[0] != '{') || !json_str(ln, "pi", pi, sizeof pi))
  {
    return;
  }
  if (strcmp(pi, g_radio.id))           // autre station : nouveau tableau de bord
  {
    memset(&g_radio, 0, sizeof g_radio);
    snprintf(g_radio.id, sizeof g_radio.id, "%s", pi);
  }
  o = obj_get(O_SERVICE, pi);
  snprintf(o->kind, sizeof o->kind, "%s", pi);
  o->mhz = atof(dec_freq(d));
  if (json_str(ln, "long_ps", ps, sizeof ps) || json_str(ln, "ps", ps, sizeof ps))
  {
    radio_txt(ps);
    if (ps[0] && strcmp(ps, g_radio.name))
    {
      snprintf(g_radio.name, sizeof g_radio.name, "%s", ps);
      snprintf(o->name, sizeof o->name, "%s", ps);
      snprintf(m, sizeof m, "%s MHz  PI %s", dec_freq(d), pi);
      add(d, C_GREEN, ps, m);
    }
  }
  if (json_str(ln, "radiotext", rt, sizeof rt))
  {
    radio_txt(rt);
    if (rt[0] && strcmp(rt, g_radio.text))
    {
      snprintf(g_radio.text, sizeof g_radio.text, "%s", rt);
      snprintf(o->info, sizeof o->info, "%s", rt);
      add(d, C_WHITE, g_radio.name[0] ? g_radio.name : pi, rt);
    }
  }
  if (json_str(ln, "prog_type", pty, sizeof pty))
  {
    radio_txt(pty);
    if (!strcmp(pty, "No PTY")) pty[0] = 0;
    snprintf(g_radio.pty, sizeof g_radio.pty, "%s", pty);
    snprintf(o->pty, sizeof o->pty, "%s", pty);
  }
  if (json_num(ln, "tp", &v)) g_radio.tp = v > 0;
  if (json_num(ln, "ta", &v)) g_radio.ta = v > 0;
  // frequences alternatives (kHz) : "AF 90.4 90.6 ..."
  if ((af = json_find(ln, "alt_frequencies_a", NULL)) && (*af == '['))
  {
    char *e;
    snprintf(g_radio.extra, sizeof g_radio.extra, "AF");
    for (af++; *af && (*af != ']'); af = e)
    {
      long k = strtol(af, &e, 10);
      char one[12];
      if (e == af) { e = (char *)af + 1; continue; }
      snprintf(one, sizeof one, " %.1f", k / 1000.0);
      if (strlen(g_radio.extra) + strlen(one) < sizeof g_radio.extra) strcat(g_radio.extra, one);
    }
  }
  // heure : "2026-11-08T12:06:00+01:00" (ou "...Z"), envoyee au debut de la minute
  if (json_str(ln, "clock_time", ct, sizeof ct))
  {
    struct tm tm;
    int y, mo, dd, h, mi, se = 0, oh = 0, om = 0;
    char sg = 'Z';
    memset(&tm, 0, sizeof tm);
    if (sscanf(ct, "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &mo, &dd, &h, &mi, &se, &sg, &oh, &om) >= 6)
    {
      int lto = (sg == '-' ? -1 : 1) * (oh * 60 + om);
      if (sg == 'Z') lto = 0;
      tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = dd; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
      radio_time(timegm(&tm) - lto * 60, lto, RDS_LATENCY);
    }
  }
}

// md_radio.py (welle-cli, mux.json) : lignes "ens", "time", "svc", "msg"
static void parse_dab(const md_decoder *d, char *ln)
{
  char type[8] = "", s[200], m[260];
  double v;
  if ((ln[0] != '{') || !json_str(ln, "type", type, sizeof type))
  {
    return;
  }
  if (!strcmp(type, "ens"))
  {
    char ch[8] = "";
    json_str(ln, "label", s, sizeof s);
    radio_txt(s);
    if (s[0] && strcmp(s, g_radio.name))
    {
      snprintf(m, sizeof m, "Channel %s  %s MHz", dab_channel(dec_freq(d)) ? dab_channel(dec_freq(d)) : "?",
               dec_freq(d));
      add(d, C_GREEN, s, m);
    }
    snprintf(g_radio.name, sizeof g_radio.name, "%s", s);
    json_str(ln, "id", g_radio.id, sizeof g_radio.id);
    json_str(ln, "chan", ch, sizeof ch);
    snprintf(g_radio.extra, sizeof g_radio.extra, "Channel %s  %s MHz", ch, dec_freq(d));
    if (json_num(ln, "snr", &v))
    {
      snprintf(s, sizeof s, "  SNR %.1f dB", v);
      strncat(g_radio.extra, s, sizeof g_radio.extra - strlen(g_radio.extra) - 1);
    }
    if (json_num(ln, "n", &v))
    {
      snprintf(s, sizeof s, "  %.0f services", v);
      strncat(g_radio.extra, s, sizeof g_radio.extra - strlen(g_radio.extra) - 1);
    }
  }
  else if (!strcmp(type, "time") && json_num(ln, "utc", &v))
  {
    double lto = 0;
    json_num(ln, "lto", &lto);
    radio_time((time_t)v, (int)lto, DAB_LATENCY);
  }
  else if (!strcmp(type, "svc"))
  {
    char sid[12] = "", dls[96] = "";
    md_obj *o;
    if (!json_str(ln, "sid", sid, sizeof sid)) return;
    o = obj_get(O_SERVICE, sid);
    if (json_str(ln, "label", s, sizeof s)) { radio_txt(s); snprintf(o->name, sizeof o->name, "%s", s); }
    if (json_str(ln, "pty", s, sizeof s)) { radio_txt(s); snprintf(o->pty, sizeof o->pty, "%s", s); }
    if (json_str(ln, "kind", s, sizeof s)) snprintf(o->kind, sizeof o->kind, "%s", s);
    if (json_num(ln, "kbps", &v) && (v > 0))
    {
      snprintf(s, sizeof s, " %.0fk", v);
      strncat(o->kind, s, sizeof o->kind - strlen(o->kind) - 1);
    }
    if (json_str(ln, "dls", dls, sizeof dls))
    {
      radio_txt(dls);
      if (dls[0] && strcmp(dls, o->info)) add(d, C_WHITE, o->name[0] ? o->name : sid, dls);
      snprintf(o->info, sizeof o->info, "%s", dls);
    }
  }
  else if (!strcmp(type, "msg") && json_str(ln, "text", s, sizeof s))
  {
    radio_txt(s);
    snprintf(g_status, sizeof g_status, "%s", s);
    g_dirty = true;
  }
}

// ---------------------------------------------------------------- aiguillage

void parse_line(const md_decoder *d, char *line)
{
  const unsigned char *p;
  bool high = false;
  line[strcspn(line, "\r\n")] = 0;
  if (!line[0])
  {
    return;
  }
  for (p = (const unsigned char *)line; *p; p++) if (*p >= 0x80) high = true;
  switch (d->parser)
  {
  case P_MULTIMON: parse_multimon(d, line); break;
  case P_APRS:     parse_aprs(d, line); break;
  case P_SONDE:    parse_sonde(d, line); break;
  case P_RTL433:   parse_rtl433(d, line); break;
  case P_AIS:      parse_ais(d, line); break;
  case P_ACARS:    parse_acars(d, line); break;
  case P_VDL2:     parse_vdl2(d, line); break;
  case P_DSD:      parse_dsd(d, line); break;
  case P_FT8:      parse_ft8(d, line); break;
  case P_WSPR:     parse_wspr(d, line); break;
  case P_RDS:      parse_rds(d, line); break;
  case P_DAB:      parse_dab(d, line); break;
  default: break;
  }
  if (high)
  {
    int i;
    for (i = 0; i < g_nobj; i++)
    {
      ascii_fold(g_obj[i].name);
      ascii_fold(g_obj[i].info);
      ascii_fold(g_obj[i].extra);
    }
  }
}

// Texte en continu (RTTY par minimodem, CW par multimon-ng). minimodem
// signale la porteuse sur stderr (melange a stdout par run_decoder.sh) par
// des lignes "### CARRIER 50 @ 1585.0 Hz ###" et "### NOCARRIER ... ###" ;
// le reste est le texte decode, caractere par caractere.
// Selcall : multimon-ng ecrit les chiffres au fil de l'eau ("ZVEI1: 123"
// puis "45"), sans fin de ligne ; les lignes JSON (DTMF) sont completes.
// Le texte est regroupe et decoupe par signalisation apres 1,5 s de silence.
static char sel_tb[1024], sel_js[512];
static int sel_n, sel_jn = -1;
static time_t sel_last;

static void selcall_bytes(const md_decoder *d, const char *buf, int n)
{
  int i;
  for (i = 0; i < n; i++)
  {
    char c = buf[i];
    if (sel_jn >= 0)
    {
      if ((c == '\n') || (sel_jn >= (int)sizeof sel_js - 1))
      {
        sel_js[sel_jn] = 0;
        parse_multimon(d, sel_js);
        sel_jn = -1;
      }
      else sel_js[sel_jn++] = c;
      continue;
    }
    if (c == '{')
    {
      sel_js[0] = c;
      sel_jn = 1;
      continue;
    }
    if ((c != '\n') && (sel_n < (int)sizeof sel_tb - 1))
    {
      sel_tb[sel_n++] = c;
      sel_tb[sel_n] = 0;
      sel_last = time(NULL);
      snprintf(g_status, sizeof g_status, "%.60s", sel_tb);
      g_dirty = true;
    }
  }
}

static void selcall_flush(const md_decoder *d)
{
  static const char *names[] = {"ZVEI1", "ZVEI2", "ZVEI3", "DZVEI", "PZVEI", "CCIR", "EEA", "EIA", NULL};
  char seq[8][64];
  int i, cur = -1;
  char *p = sel_tb;
  memset(seq, 0, sizeof seq);
  while (*p)
  {
    int k, hit = -1;
    for (k = 0; names[k]; k++)
    {
      int l = strlen(names[k]);
      if (!strncmp(p, names[k], l) && (p[l] == ':'))
      {
        hit = k;
        p += l + 1;
        break;
      }
    }
    if (hit >= 0)
    {
      cur = hit;
      continue;
    }
    if ((cur >= 0) && isalnum((unsigned char)*p) && (strlen(seq[cur]) < sizeof seq[0] - 1))
    {
      strncat(seq[cur], p, 1);
    }
    p++;
  }
  for (i = 0; names[i]; i++)
  {
    if (seq[i][0]) add(d, C_WHITE, names[i], seq[i]);
  }
  sel_n = 0;
  sel_tb[0] = 0;
  g_status[0] = 0;
}

void parse_bytes(const md_decoder *d, const char *buf, int n)
{
  static char st[200];
  static int stn = -1;                   // >= 0 : dans une ligne "### ... "
  static int hashes;
  int i;
  if (d->parser == P_SELCALL)
  {
    selcall_bytes(d, buf, n);
    return;
  }
  for (i = 0; i < n; i++)
  {
    char c = buf[i];
    if (stn >= 0)
    {
      if ((c != '\n') && (stn < (int)sizeof st - 1))
      {
        st[stn++] = c;
        continue;
      }
      st[stn] = 0;
      stn = -1;
      if (d->parser == P_CW)               // md_cw : "### Tone 700 Hz   22 WPM   S/N 18 dB"
      {
        snprintf(g_status, sizeof g_status, "%s", st);
        trim(g_status);
        g_dirty = true;
        continue;
      }
      if (strstr(st, "NOCARRIER"))
      {
        snprintf(g_status, sizeof g_status, "No carrier");
        stream_put("\n", 1);
      }
      else if (strstr(st, "CARRIER"))
      {
        char *at = strchr(st, '@');
        snprintf(g_status, sizeof g_status, "Carrier %s", at ? at + 1 : "");
        g_status[strcspn(g_status, "#")] = 0;
        trim(g_status);
        log_line(d, "Carrier", g_status);
      }
      g_dirty = true;
      continue;
    }
    if (((d->parser == P_MINIMODEM) || (d->parser == P_CW)) && (c == '#'))
    {
      if (++hashes == 3)
      {
        stn = 0;
        hashes = 0;
      }
      continue;
    }
    if (hashes)
    {
      stream_put("##", hashes);
      hashes = 0;
    }
    stream_put(&c, 1);
  }
}

void parse_poll(const md_decoder *d)
{
  time_t now = time(NULL);
  if (d->parser == P_ADSB)
  {
    poll_adsb(d);
  }
  if (sel_n && (now - sel_last >= 2))
  {
    selcall_flush(d);
  }
  if (dtmf_seq[0] && (now - dtmf_last >= 2))
  {
    add(d, C_WHITE, "DTMF", dtmf_seq);
    dtmf_seq[0] = 0;
    g_status[0] = 0;
  }
}
