// multidec.c : decodeur multiple de Portsdown 4.
//
// Lance depuis le Menu 5 de rpidatvgui (code 152, scheduler.sh). Ecrans, tous
// dessines comme la GUI (touches en relief de button3d.h) :
//   - menu : choix du decodeur, frequence, gain, correction, options ;
//   - options du decodeur, pave numerique, configuration, journal ;
//   - reception : messages, tableau et radar, radiosonde, ou texte continu.
// La reception elle-meme est faite par run_decoder.sh (rtl_fm, multimon-ng,
// direwolf, readsb...) dont la sortie est lue ici. Les positions sont
// ecrites dans /dev/shm/multidec/state.json pour la carte web (nginx).
// Sortie : 129 (EXIT, menu principal) ou 205 (DATV, Menu 5).

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/wait.h>
#include <unistd.h>
#include "md.h"

extern char *fbp;                       // Graphics.h (md_draw.c)

enum { S_MENU, S_OPTIONS, S_KEYPAD, S_RUN, S_LOG, S_CONFIG };

enum {
  B_DEC = 0,                            // 0..14 : decodeurs
  B_RECEIVE = 100, B_FREQ, B_GAIN, B_PPM, B_OPTIONS, B_LOG, B_DATV, B_EXIT, B_CONFIG, B_BACK, B_MPAGE,
  B_OPT = 200,                          // 200 + 10 * rang + valeur
  B_PRESET = 250,
  B_PAUSE = 300, B_ACTION, B_VIEW, B_MENU, B_AUDIO,
  B_KEY = 400,                          // 400 + caractere
  B_KDEL = 600, B_KCLR, B_KOK, B_KCANCEL, B_KAUTO,
  B_OLDER = 700, B_NEWER,
  B_POSLOC = 800, B_POSMAN, B_LAT, B_LON, B_HFV4, B_HFDIRECT, B_LOGONOFF,
  B_LIST_UP = 900, B_LIST_DOWN
};

typedef struct { int x, y, w, h, id; } button;

static button btns[40];
static int nbtn;
static char *chrome;                    // partie fixe de l'ecran (titre, touches)
static int screen = S_MENU, kp_return = S_MENU;
static const md_decoder *dec;
static pid_t child;
static int child_fd = -1;
static char linebuf[16384];
static int linelen;
static bool paused, show_table, scanning;
static int scroll_off;                   // messages sautes (pause)
static int range_idx = 1;
static char dab_playing[24];             // service DAB joue sur le Pi (md_radio.py lit MD_SHM/dab_play)
static int menu_page = -1;               // page des decodeurs du menu (-1 : celle du decodeur choisi)
static char kp_value[24], kp_title[64], kp_target[16];
static bool kp_fresh;                    // valeur initiale pas encore modifiee
static char level_txt[32];
static float level_q = -1;              // 0..1, < 0 : pas de barre
static char log_lines[300][200];
static int log_n, log_off;
static bool redraw_static = true;
static char myip[32] = "";

// Horloge monotone en millisecondes (64 bits : time_t fait 32 bits sur le Pi)
static long long now_ms(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// ---------------------------------------------------------------- touches

static void add_btn(int x, int y, int w, int h, int id, const char *col, const char *label, int big)
{
  draw_button(x, y, w, h, col, label, big);
  if (nbtn < (int)(sizeof btns / sizeof btns[0]))
  {
    btns[nbtn].x = x; btns[nbtn].y = y; btns[nbtn].w = w; btns[nbtn].h = h; btns[nbtn].id = id;
    nbtn++;
  }
}

static void menu_btn(int pos, int id, const char *col, const char *label)
{
  int x, y, w, h;
  menu_rect(pos, &x, &y, &w, &h);
  add_btn(x, y, w, h, id, col, label, 1);
}

static int hit(int x, int y)
{
  int i;
  for (i = 0; i < nbtn; i++)
  {
    if ((x >= btns[i].x) && (x < btns[i].x + btns[i].w) && (y >= btns[i].y) && (y < btns[i].y + btns[i].h))
    {
      return btns[i].id;
    }
  }
  return -1;
}

// ---------------------------------------------------------------- utilitaires

static const char *ckey(const char *k)
{
  static char s[64];
  snprintf(s, sizeof s, "%s_%s", dec->key, k);
  return s;
}

static const char *age_str(time_t t)
{
  static char s[16];
  long a = time(NULL) - t;
  if (a < 60) snprintf(s, sizeof s, "%lds", a);
  else if (a < 3600) snprintf(s, sizeof s, "%ldm", a / 60);
  else snprintf(s, sizeof s, "%ldh", a / 3600);
  return s;
}

// Debut de s qui tient dans maxw pixels (out peut etre s lui-meme)
static void fit_copy(char *out, int size, int font, const char *s, int maxw)
{
  int n = text_fit(font, s, maxw);
  if (n > size - 1) n = size - 1;
  memmove(out, s, n);
  out[n] = 0;
}

// Coupe s en lignes de largeur maxw (aux espaces si possible)
static int wrap(const char *s, int font, int maxw, char out[][160], int maxl)
{
  int nl = 0;
  while (*s && (nl < maxl))
  {
    int n = text_fit(font, s, maxw), k = n;
    if (n >= (int)strlen(s))
    {
      snprintf(out[nl++], 160, "%s", s);
      break;
    }
    while ((k > 0) && (s[k] != ' ')) k--;
    if (k == 0) k = n;
    if (k > 158) k = 158;
    snprintf(out[nl++], 160, "%.*s", k, s);
    s += k;
    while (*s == ' ') s++;
  }
  return nl;
}

// Horloge du Pi synchronisee par NTP (etat du noyau, mis par timesyncd)
static bool ntp_synced(void)
{
  struct timex tx;
  memset(&tx, 0, sizeof tx);
  return (adjtimex(&tx) != TIME_ERROR) && !(tx.status & STA_UNSYNC);
}

// Le decodeur sort-il du son (haut-parleur, page web) ? DAB : le service
// touche ; les autres : leur option Audio
static bool audio_out(void)
{
  int i;
  if (dec->parser == P_DAB) return true;
  for (i = 0; i < MAX_OPTS; i++)
  {
    if (dec->opt[i].key && !strcmp(dec->opt[i].key, "audio")) return strcmp(dec_opt(dec, i), "off") != 0;
  }
  return false;
}

// Touche Audio : haut-parleur coupe si MD_SHM/mute existe (md_audio le lit en
// direct, sans relancer la chaine)
static bool audio_muted(void)
{
  return !strcmp(conf_get("audio_mute", "off"), "on");
}

static void apply_mute(void)
{
  if (audio_muted())
  {
    FILE *f = fopen(MD_SHM "/mute", "w");
    if (f) fclose(f);
  }
  else
  {
    unlink(MD_SHM "/mute");
  }
}

// Bande balayee par la touche Scan du decodeur
static const char *scan_band(void)
{
  if (dec->parser == P_RDS) return "FM 87.5-108 MHz";
  if (dec->parser == P_DAB) return "Band III";
  return "400-406 MHz";
}

static void station_update(void)
{
  char loc[16] = "";
  if (!strcmp(conf_get("station", "locator"), "manual"))
  {
    g_slat = atof(conf_get("station_lat", "nan"));
    g_slon = atof(conf_get("station_lon", "nan"));
    return;
  }
  pconf_get(MD_LOCATORS, "mylocator", loc, sizeof loc);
  if (!locator_to_ll(loc, &g_slat, &g_slon))
  {
    g_slat = g_slon = NAN;
  }
}

static void find_ip(void)
{
  struct ifaddrs *ifa, *i;
  myip[0] = 0;
  if (getifaddrs(&ifa)) return;
  for (i = ifa; i; i = i->ifa_next)
  {
    if (i->ifa_addr && (i->ifa_addr->sa_family == AF_INET) && strcmp(i->ifa_name, "lo"))
    {
      inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, myip, sizeof myip);
      break;
    }
  }
  freeifaddrs(ifa);
}

// ---------------------------------------------------------------- processus de reception

static void stop_decoder(void)
{
  int i;
  if (child > 0)
  {
    kill(-child, SIGTERM);
    for (i = 0; i < 40; i++)
    {
      if (waitpid(child, NULL, WNOHANG) == child) { child = 0; break; }
      usleep(50000);
    }
    if (child > 0)
    {
      kill(-child, SIGKILL);
      waitpid(child, NULL, 0);
      child = 0;
    }
    usleep(200000);                      // la cle USB se libere
  }
  if (child_fd >= 0)
  {
    close(child_fd);
    child_fd = -1;
  }
  linelen = 0;
}

static void start_decoder(bool scan)
{
  int p[2], i;
  char v[32], call[16] = "", loc[16] = "";
  stop_decoder();
  mkdir(MD_SHM, 0755);
  unlink(MD_SHM "/level");
  setenv("MD_DEC", dec->key, 1);
  setenv("MD_FREQ", dec_freq(dec), 1);
  setenv("MD_GAIN", dec_gain(dec), 1);
  setenv("MD_PPM", conf_get("ppm", "0"), 1);
  setenv("MD_HF", conf_get("hf_input", "v4"), 1);
  setenv("MD_SHM", MD_SHM, 1);
  setenv("MD_SCAN", scan ? "1" : "0", 1);
  setenv("MD_CHAN", dab_channel(dec_freq(dec)), 1);
  apply_mute();
  unlink(MD_SHM "/dab_play");            // DAB : rien n'est joue sur le Pi au depart
  unlink(MD_SHM "/sonde_bw");            // radiosonde : largeur d'ecoute de depart (run_decoder.sh)
  dab_playing[0] = 0;
  for (i = 0; i < MAX_OPTS; i++)
  {
    char k[40];
    if (!dec->opt[i].key) continue;
    snprintf(k, sizeof k, "MD_OPT_%s", dec->opt[i].key);
    setenv(k, dec_opt(dec, i), 1);
  }
  if (isfinite(g_slat))
  {
    snprintf(v, sizeof v, "%.5f", g_slat); setenv("MD_LAT", v, 1);
    snprintf(v, sizeof v, "%.5f", g_slon); setenv("MD_LON", v, 1);
  }
  else
  {
    unsetenv("MD_LAT");
    unsetenv("MD_LON");
  }
  pconf_get(MD_PCONFIG, "callsign", call, sizeof call);
  pconf_get(MD_LOCATORS, "mylocator", loc, sizeof loc);
  loc[6] = 0;
  setenv("MD_CALL", call, 1);
  setenv("MD_LOC", loc, 1);

  if (pipe(p))
  {
    return;
  }
  child = fork();
  if (child == 0)
  {
    char logp[80];
    int lf;
    setsid();
    dup2(p[1], 1);
    snprintf(logp, sizeof logp, "/tmp/multidec_%s.log", dec->key);
    lf = open(logp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (lf >= 0) dup2(lf, 2);
    for (i = 3; i < 256; i++) close(i);
    signal(SIGPIPE, SIG_DFL);
    execl("/bin/bash", "bash", MD_RUN, (char *)NULL);
    _exit(127);
  }
  close(p[1]);
  child_fd = p[0];
  fcntl(child_fd, F_SETFL, O_NONBLOCK);
  scanning = scan;
  if (scan)
  {
    snprintf(g_status, sizeof g_status, "Scanning %s", scan_band());
  }
}

// Fin de la chaine : les dernieres lignes de son journal, en rouge
static void decoder_died(void)
{
  char logp[80], ln[256], last[3][256] = {"", "", ""};
  int n = 0, i;
  FILE *f;
  waitpid(child, NULL, WNOHANG);
  child = 0;
  close(child_fd);
  child_fd = -1;
  snprintf(logp, sizeof logp, "/tmp/multidec_%s.log", dec->key);
  f = fopen(logp, "r");
  if (f)
  {
    while (fgets(ln, sizeof ln, f))
    {
      ln[strcspn(ln, "\r\n")] = 0;
      if (ln[0]) { snprintf(last[n % 3], sizeof last[0], "%s", ln); n++; }
    }
    fclose(f);
  }
  for (i = (n > 3 ? n - 3 : 0); i < n; i++)
  {
    msg_add(C_RED, "Decoder output", last[i % 3]);
  }
  msg_add(C_RED, "Decoder stopped", logp);
}

static void handle_line(char *ln)
{
  char title[48];
  snprintf(title, sizeof title, "Scan %s", scan_band());
  if (scanning && !strncmp(ln, "SCAN ", 5))
  {
    // "SCAN <MHz> <dB> <nom>" : on ecoute cette frequence
    char f[16] = "", s[16] = "", m[200];
    int k = 0;
    sscanf(ln + 5, "%15s %15s %n", f, s, &k);
    conf_set(ckey("freq"), f);
    snprintf(m, sizeof m, "%s detected on %s MHz (+%s dB above noise)", k && ln[5 + k] ? ln + 5 + k : "Signal", f, s);
    msg_add(C_AMBER, title, m);
    g_status[0] = 0;
    redraw_static = true;
    start_decoder(false);
    return;
  }
  if (scanning && !strcmp(ln, "SCANEND"))
  {
    msg_add(C_AMBER, title, "Nothing found");
    g_status[0] = 0;
    redraw_static = true;
    start_decoder(false);
    return;
  }
  if (scanning && !strncmp(ln, "FOUND ", 6))
  {
    // "FOUND <MHz> <cle> <nom>" : station RDS (cle : PI) ou multiplex DAB (canal)
    char f[16] = "", key[16] = "", m[200];
    int k = 0;
    sscanf(ln + 6, "%15s %15s %n", f, key, &k);
    if (dec->parser == P_RDS)
    {
      md_obj *o = obj_get(O_SERVICE, key);
      snprintf(o->kind, sizeof o->kind, "%s", key);
      if (k) snprintf(o->name, sizeof o->name, "%s", ln + 6 + k);
      ascii_fold(o->name);
      o->mhz = atof(f);
    }
    snprintf(m, sizeof m, "%s MHz  %s  %s", f, key, k ? ln + 6 + k : "");
    msg_add(C_GREEN, title, m);
    g_dirty = true;
    return;
  }
  if (scanning)
  {
    if (!strncmp(ln, "SCANNING ", 9)) snprintf(g_status, sizeof g_status, "Scanning : %.100s", ln + 9);
    else if (dec->parser != P_RDS && dec->parser != P_DAB) snprintf(g_status, sizeof g_status, "Scanning %s", scan_band());
    g_dirty = true;
    return;
  }
  parse_line(dec, ln);
}

static void read_child(void)
{
  char buf[4096];
  ssize_t r;
  while ((r = read(child_fd, buf, sizeof buf)) > 0)
  {
    int i;
    if (paused)
    {
      continue;                          // en pause : la sortie est ignoree
    }
    if ((dec->parser == P_MINIMODEM) || (dec->parser == P_CW) || (dec->parser == P_SELCALL))
    {
      parse_bytes(dec, buf, r);
      continue;
    }
    for (i = 0; i < r; i++)
    {
      if (buf[i] == '\n')
      {
        linebuf[linelen] = 0;
        handle_line(linebuf);
        linelen = 0;
      }
      else if (linelen < (int)sizeof linebuf - 1)
      {
        linebuf[linelen++] = buf[i];
      }
    }
  }
  if (r == 0)
  {
    decoder_died();
  }
}

static void read_level(void)
{
  float a, b;
  FILE *f;
  level_q = -1;
  if (!dec->audio || scanning)          // balayage : pas d'audio (rtl_power)
  {
    return;
  }
  f = fopen(MD_SHM "/level", "r");
  if (!f)
  {
    level_q = 0;
    snprintf(level_txt, sizeof level_txt, "-");
    return;
  }
  if (fscanf(f, "%f %f", &a, &b) == 2)
  {
    if (dec->fm)
    {
      // FM : le souffle (aigus) baisse quand une porteuse arrive
      // sans porteuse : -15 a -16 dB mesures sur le Pi (144,8 et 439,9875 MHz)
      level_q = fminf(fmaxf((-16.0f - b) / 14.0f, 0), 1);
      snprintf(level_txt, sizeof level_txt, "%.0f dB", b);
    }
    else
    {
      level_q = fminf(fmaxf((a + 70.0f) / 60.0f, 0), 1);
      snprintf(level_txt, sizeof level_txt, "%.0f dBFS", a);
    }
  }
  fclose(f);
}

// ---------------------------------------------------------------- carte web

static void jstr(FILE *f, const char *s)
{
  fputc('"', f);
  for (; *s; s++)
  {
    unsigned char c = *s;
    if ((c == '"') || (c == '\\')) fprintf(f, "\\%c", c);
    else if (c < 32) fputc(' ', f);
    else if (c >= 128) fprintf(f, "\\u%04x", c);
    else fputc(c, f);
  }
  fputc('"', f);
}

static long obj_timeout(int type)
{
  switch (type)
  {
  case O_AIRCRAFT: return 90;
  case O_SHIP: return 1200;
  case O_SONDE: return 1800;
  case O_STATION: return 1800;
  default: return 3600;
  }
}

static void export_state(void)
{
  static const char *tn[] = {"none", "aircraft", "ship", "aprs", "sonde", "station", "sensor"};
  time_t now = time(NULL);
  FILE *f = fopen(MD_SHM "/state.json.tmp", "w");
  int i, k, first = 1;
  if (!f)
  {
    return;
  }
  fprintf(f, "{\"decoder\":\"%s\",\"title\":", dec->key);
  jstr(f, dec->title);
  fprintf(f, ",\"freq\":\"%s\",\"time\":%ld,\"running\":%s", dec_freq(dec), (long)now,
          (child > 0 && screen == S_RUN) ? "true" : "false");
  if (isfinite(g_slat)) fprintf(f, ",\"station\":{\"lat\":%.5f,\"lon\":%.5f}", g_slat, g_slon);
  fprintf(f, ",\"objects\":[");
  for (i = 0; i < g_nobj; i++)
  {
    md_obj *o = &g_obj[i];
    if (!o->pos || (now - o->seen > obj_timeout(o->type))) continue;
    fprintf(f, "%s{\"type\":\"%s\",\"id\":", first ? "" : ",", tn[o->type]);
    jstr(f, o->id);
    fprintf(f, ",\"name\":");
    jstr(f, o->name);
    if (o->sym[0])
    {
      fprintf(f, ",\"sym\":");
      jstr(f, o->sym);
    }
    fprintf(f, ",\"info\":");
    jstr(f, o->info);
    fprintf(f, ",\"lat\":%.5f,\"lon\":%.5f,\"age\":%ld", o->lat, o->lon, (long)(now - o->seen));
    if (isfinite(o->alt)) fprintf(f, ",\"alt\":%.0f", o->alt);
    if (isfinite(o->spd)) fprintf(f, ",\"spd\":%.1f", o->spd);
    if (isfinite(o->crs)) fprintf(f, ",\"crs\":%.0f", o->crs);
    if (isfinite(o->snr)) fprintf(f, ",\"snr\":%.1f", o->snr);
    fprintf(f, ",\"trail\":[");
    for (k = (o->ntrail > TRAIL ? o->ntrail - TRAIL : 0); k < o->ntrail; k++)
    {
      fprintf(f, "%s[%.5f,%.5f]", k > (o->ntrail > TRAIL ? o->ntrail - TRAIL : 0) ? "," : "",
              o->trail[k % TRAIL][0], o->trail[k % TRAIL][1]);
    }
    fprintf(f, "]}");
    first = 0;
  }
  // capteurs ISM (sans position) : tableau de la page web, comme a l'ecran
  fprintf(f, "],\"sensors\":[");
  first = 1;
  for (i = 0; i < g_nobj; i++)
  {
    md_obj *o = &g_obj[i];
    if ((o->type != O_SENSOR) || (now - o->seen > obj_timeout(o->type))) continue;
    fprintf(f, "%s{\"model\":", first ? "" : ",");
    jstr(f, o->name);
    fprintf(f, ",\"id\":");
    jstr(f, o->extra);
    fprintf(f, ",\"info\":");
    jstr(f, o->info);
    if (isfinite(o->snr)) fprintf(f, ",\"rssi\":%.1f", o->snr);
    fprintf(f, ",\"age\":%ld,\"count\":%ld}", (long)(now - o->seen), o->count);
    first = 0;
  }
  fprintf(f, "]");
  // radio (RDS, DAB+) : station ou multiplex en cours, heure recue, services
  if (dec->view == V_RADIO)
  {
    fprintf(f, ",\"radio\":{\"kind\":\"%s\",\"name\":", dec->key);
    jstr(f, g_radio.name);
    fprintf(f, ",\"id\":");
    jstr(f, g_radio.id);
    fprintf(f, ",\"pty\":");
    jstr(f, g_radio.pty);
    fprintf(f, ",\"text\":");
    jstr(f, g_radio.text);
    fprintf(f, ",\"extra\":");
    jstr(f, g_radio.extra);
    fprintf(f, ",\"tp\":%s,\"ta\":%s,\"ntp\":%s", g_radio.tp ? "true" : "false", g_radio.ta ? "true" : "false",
            ntp_synced() ? "true" : "false");
    if (g_radio.got)
    {
      fprintf(f, ",\"utc\":%ld,\"lto\":%d,\"diff\":%.2f,\"tage\":%ld", (long)g_radio.utc, g_radio.lto,
              g_radio.diff, (long)(now - g_radio.got));
    }
    fprintf(f, ",\"services\":[");
    first = 1;
    for (i = 0; i < g_nobj; i++)
    {
      md_obj *o = &g_obj[i];
      if ((o->type != O_SERVICE) || (now - o->seen > obj_timeout(o->type))) continue;
      fprintf(f, "%s{\"id\":", first ? "" : ",");
      jstr(f, o->id);
      fprintf(f, ",\"name\":");
      jstr(f, o->name);
      fprintf(f, ",\"pty\":");
      jstr(f, o->pty);
      fprintf(f, ",\"kind\":");
      jstr(f, o->kind);
      fprintf(f, ",\"info\":");
      jstr(f, o->info);
      if (o->mhz > 0) fprintf(f, ",\"mhz\":%.3f", o->mhz);
      fprintf(f, ",\"age\":%ld}", (long)(now - o->seen));
      first = 0;
    }
    fprintf(f, "]}");
  }
  fprintf(f, ",\"track\":{\"id\":");
  jstr(f, g_track_id);
  fprintf(f, ",\"points\":[");
  for (i = 0; i < g_ntrack; i++)
  {
    fprintf(f, "%s[%.5f,%.5f,%.0f]", i ? "," : "", g_track[i].lat, g_track[i].lon, g_track[i].alt);
  }
  fprintf(f, "]}");
  // vue de la page web : "list" (messages, comme l'ecran : POCSAG, ACARS...),
  // "stream" (texte continu : CW, RTTY), sinon la carte ou les tableaux
  {
    static const char *vn[] = {"list", "objects", "sonde", "stream", "sensor", "radio"};
    fprintf(f, ",\"view\":\"%s\",\"status\":", vn[dec->view]);
    jstr(f, g_status);
    fprintf(f, ",\"messages\":[");
    for (k = 0; k < g_nmsg && k < 60; k++)
    {
      md_msg *m = &g_msg[(g_msghead - 1 - k + MSG_MAX * 2) % MSG_MAX];
      fprintf(f, "%s{\"t\":%ld,\"head\":", k ? "," : "", (long)m->t);
      jstr(f, m->head);
      fprintf(f, ",\"body\":");
      jstr(f, m->body);
      fprintf(f, "}");
    }
    fprintf(f, "]");
    if (dec->view == V_STREAM)
    {
      fprintf(f, ",\"stream\":[");
      for (k = 0; k < g_stream_lines; k++)
      {
        fprintf(f, "%s", k ? "," : "");
        jstr(f, g_stream[k]);
      }
      fprintf(f, "]");
    }
  }
  fprintf(f, "}\n");
  fclose(f);
  rename(MD_SHM "/state.json.tmp", MD_SHM "/state.json");
}

// ---------------------------------------------------------------- ecrans fixes

static void build_menu(void)
{
  char s[64];
  int i;
  const char *fq = dec_freq(dec), *g = dec_gain(dec);
  draw_title("Multi Decoder Menu");
  {
    // 15 cases ; au-dela de 15 decodeurs : 14 par page, la 15e change de page
    int per = (NDECODERS > 15) ? 14 : 15, pages = (NDECODERS + per - 1) / per, k;
    if (menu_page >= pages) menu_page = 0;
    if (menu_page < 0) menu_page = (int)(dec - DECODERS) / per;
    for (k = 0; k < per && menu_page * per + k < NDECODERS; k++)
    {
      int pos = (k < 5) ? 15 + k : ((k < 10) ? 5 + k : k - 5);
      i = menu_page * per + k;
      menu_btn(pos, B_DEC + i, (&DECODERS[i] == dec) ? "Green" : "Blue", DECODERS[i].button);
    }
    if (pages > 1) menu_btn(9, B_MPAGE, "Slate", (menu_page + 1 < pages) ? "More^decoders" : "Back^to page 1");
  }
  menu_btn(0, B_RECEIVE, "Green", "RECEIVE");
  snprintf(s, sizeof s, "Freq^%s", fq);
  if (!strcmp(dec->key, "ais")) snprintf(s, sizeof s, "Freq^161.975/.025");
  if (!strcmp(dec->key, "acars")) snprintf(s, sizeof s, "Freq^131.5-131.8");
  if (!strcmp(dec->key, "vdl2")) snprintf(s, sizeof s, "Freq^136.7-136.9");
  menu_btn(1, B_FREQ, dec->fixed ? "Grey" : "Blue", s);
  snprintf(s, sizeof s, "Gain^%s", strcmp(g, "auto") ? g : "Auto");
  menu_btn(2, B_GAIN, "Blue", s);
  snprintf(s, sizeof s, "PPM^%s", conf_get("ppm", "0"));
  menu_btn(3, B_PPM, "Blue", s);
  snprintf(s, sizeof s, "%s^Options", dec->title);
  menu_btn(4, B_OPTIONS, dec->opt[0].key ? "Blue" : "Grey", s);
  menu_btn(20, B_LOG, "Blue", "Decoded^Log");
  menu_btn(21, B_DATV, "Slate", "DATV");
  menu_btn(22, B_EXIT, "Slate", "EXIT");
  menu_btn(23, B_CONFIG, "Slate", "Config");
}

static void build_options(void)
{
  static const int base[MAX_OPTS] = {15, 10, 5, 0};
  char s[80];
  int i, v;
  snprintf(s, sizeof s, "%s Options", dec->title);
  draw_title(s);
  for (i = 0; i < MAX_OPTS; i++)
  {
    const md_opt *o = &dec->opt[i];
    const char *cur;
    if (!o->key) continue;
    cur = dec_opt(dec, i);
    for (v = 0; v < MAX_VALS && o->vals[v]; v++)
    {
      const char *shown = o->shown[v] ? o->shown[v] : o->vals[v];
      snprintf(s, sizeof s, "%s^%s", o->label, shown);
      menu_btn(base[i] + v, B_OPT + i * 10 + v, strcmp(cur, o->vals[v]) ? "Blue" : "Green", s);
    }
  }
  for (i = 0; i < 2; i++)
  {
    if (dec->preset[i][0]) menu_btn(20 + i, B_PRESET + i, "Blue", dec->preset[i][0]);
  }
  menu_btn(22, B_EXIT, "Slate", "EXIT");
  menu_btn(23, B_BACK, "Slate", "Back");
}

static void build_keypad(void)
{
  static const char *keys[4][3] = {{"7", "8", "9"}, {"4", "5", "6"}, {"1", "2", "3"}, {"0", ".", "-"}};
  int r, c;
  draw_title(kp_title);
  for (r = 0; r < 4; r++)
  {
    for (c = 0; c < 3; c++)
    {
      const char *k = keys[r][c];
      if (!strcmp(k, "-") && !strcmp(kp_target, "gain"))
      {
        add_btn(150 + c * 130, 130 + r * 86, 120, 76, B_KAUTO, "Blue", "Auto", 1);
        continue;
      }
      add_btn(150 + c * 130, 130 + r * 86, 120, 76, B_KEY + k[0], "Blue", k, 1);
    }
  }
  add_btn(560, 130, 150, 76, B_KDEL, "Blue", "Del", 1);
  add_btn(560, 216, 150, 76, B_KCLR, "Blue", "Clear", 1);
  add_btn(560, 302, 150, 76, B_KCANCEL, "Slate", "Cancel", 1);
  add_btn(560, 388, 150, 76, B_KOK, "Green", "Enter", 1);
}

static void build_config(void)
{
  bool man = !strcmp(conf_get("station", "locator"), "manual");
  bool direct = !strcmp(conf_get("hf_input", "v4"), "direct");
  char s[64], loc[16] = "?";
  draw_title("Multi Decoder Config");
  pconf_get(MD_LOCATORS, "mylocator", loc, sizeof loc);
  snprintf(s, sizeof s, "Position^Locator %s", loc);
  menu_btn(15, B_POSLOC, man ? "Blue" : "Green", s);
  menu_btn(16, B_POSMAN, man ? "Green" : "Blue", "Position^Manual");
  snprintf(s, sizeof s, "Lat^%s", conf_get("station_lat", "-"));
  menu_btn(17, B_LAT, man ? "Blue" : "Grey", s);
  snprintf(s, sizeof s, "Lon^%s", conf_get("station_lon", "-"));
  menu_btn(18, B_LON, man ? "Blue" : "Grey", s);
  menu_btn(10, B_HFV4, direct ? "Blue" : "Green", "HF input^RTL-SDR V4");
  menu_btn(11, B_HFDIRECT, direct ? "Green" : "Blue", "HF input^Direct Q");
  snprintf(s, sizeof s, "Log^%s", strcmp(conf_get("log", "on"), "off") ? "ON" : "OFF");
  menu_btn(5, B_LOGONOFF, "Blue", s);
  menu_btn(22, B_EXIT, "Slate", "EXIT");
  menu_btn(23, B_BACK, "Slate", "Back");
}

static const char *rx_title(void)
{
  static char s[96];
  const char *g = dec_gain(dec), *fq = dec_freq(dec);
  char fs[40];
  snprintf(fs, sizeof fs, "%s MHz", fq);
  if (!strcmp(dec->key, "ais")) snprintf(fs, sizeof fs, "161.975 / 162.025 MHz");
  if (!strcmp(dec->key, "acars")) snprintf(fs, sizeof fs, "131.525-131.825 MHz");
  if (!strcmp(dec->key, "vdl2")) snprintf(fs, sizeof fs, "136.725-136.975 MHz");
  if (!strcmp(dec->key, "dab")) snprintf(fs, sizeof fs, "%s  %s MHz", dab_channel(fq), fq);
  snprintf(s, sizeof s, "%s  %s  Gain %s", dec->title, fs, strcmp(g, "auto") ? g : "Auto");
  return s;
}

static bool has_table(void)
{
  return (dec->view == V_OBJ) || (dec->view == V_SENSOR) || (dec->view == V_SONDE);
}

static void build_run(void)
{
  static const char *ranges[4][4] = {{"50 km", "100 km", "200 km", "400 km"}, {"25 km", "50 km", "100 km", "200 km"},
                                     {"500 km", "1000 km", "2000 km", "5000 km"}, {"10 km", "25 km", "50 km", "100 km"}};
  int rset = (dec->objtype == O_AIRCRAFT) ? 0 : ((dec->objtype == O_STATION) ? 2 : ((dec->objtype == O_SHIP) ? 3 : 1));
  char s[48];
  // touches de droite : 5 de 72 pixels, ou 6 de 58 avec la touche Audio et
  // une touche de vue
  bool view = (dec->view == V_RADIO) || has_table(), aud = audio_out(), six = view && aud;
  int step = six ? 66 : 80, h = six ? 58 : 72, y = 80 + 2 * step;
  draw_title(rx_title());
  add_btn(652, 80, 139, h, B_PAUSE, paused ? "Green" : "Blue", paused ? "Resume" : "Pause", 1);
  if ((dec->view == V_OBJ) && show_table)
  {
    snprintf(s, sizeof s, "Range^%s", ranges[rset][range_idx]);
    add_btn(652, 80 + step, 139, h, B_ACTION, "Blue", s, 1);
  }
  else if (dec->view == V_SONDE)
  {
    add_btn(652, 80 + step, 139, h, B_ACTION, scanning ? "Green" : "Blue", "Scan^400-406", 1);
  }
  else if (dec->view == V_RADIO)
  {
    add_btn(652, 80 + step, 139, h, B_ACTION, "Blue", "Set^Clock", 1);
  }
  else
  {
    add_btn(652, 80 + step, 139, h, B_ACTION, "Blue", "Clear", 1);
  }
  if (dec->view == V_RADIO)
  {
    add_btn(652, y, 139, h, B_VIEW, scanning ? "Green" : "Blue", dec->parser == P_DAB ? "Scan^Band III" : "Scan^FM", 1);
    y += step;
  }
  else if (has_table())
  {
    if (dec->view == V_SONDE) add_btn(652, y, 139, h, B_VIEW, "Blue", show_table ? "View^List" : "View^Track", 1);
    else add_btn(652, y, 139, h, B_VIEW, "Blue", show_table ? "View^List" : "View^Table", 1);
    y += step;
  }
  if (aud)
  {
    add_btn(652, y, 139, h, B_AUDIO, audio_muted() ? "Amber" : "Blue", audio_muted() ? "Audio^Muted" : "Audio^ON", 1);
  }
  add_btn(652, six ? 80 + 4 * step : 320, 139, h, B_MENU, "Slate", "Menu", 1);
  add_btn(652, six ? 80 + 5 * step : 400, 139, h, B_EXIT, "Slate", "EXIT", 1);
}

static void build_log(void)
{
  char s[80];
  snprintf(s, sizeof s, "%s Decoded Log", dec->title);
  draw_title(s);
  add_btn(652, 80, 139, 72, B_NEWER, "Blue", "Newer", 1);
  add_btn(652, 160, 139, 72, B_OLDER, "Blue", "Older", 1);
  add_btn(652, 400, 139, 72, B_BACK, "Slate", "Back", 1);
}

static void build_static(void)
{
  nbtn = 0;
  draw_clear();
  switch (screen)
  {
  case S_MENU: build_menu(); break;
  case S_OPTIONS: build_options(); break;
  case S_KEYPAD: build_keypad(); break;
  case S_CONFIG: build_config(); break;
  case S_RUN: build_run(); break;
  case S_LOG: build_log(); break;
  }
  memcpy(chrome, fbp, MD_W * MD_H * 4);
  redraw_static = false;
}

// ---------------------------------------------------------------- parties vivantes

static void draw_status(void)
{
  char right[64], ts[16];
  time_t now = time(NULL);
  struct tm tm;
  int n = 0, i;
  localtime_r(&now, &tm);
  strftime(ts, sizeof ts, "%H:%M:%S", &tm);
  if (dec->parser == P_ADSB)
  {
    for (i = 0; i < g_nobj; i++) if ((g_obj[i].type == O_AIRCRAFT) && (now - g_obj[i].seen < 60)) n++;
    snprintf(right, sizeof right, "%d aircraft   %.0f msg/min   %s", n, g_adsb_rate, ts);
  }
  else if ((dec->view == V_OBJ) || (dec->view == V_SENSOR) || (dec->view == V_SONDE))
  {
    for (i = 0; i < g_nobj; i++) if (now - g_obj[i].seen < obj_timeout(g_obj[i].type)) n++;
    snprintf(right, sizeof right, "%d seen   %d msg   %s", n, g_nmsg, ts);
  }
  else
  {
    snprintf(right, sizeof right, "%d messages   %s", g_nmsg, ts);
  }
  draw_text(640, 64, F_C16, C_HEAD, 2, right);
  if (level_q >= 0)
  {
    draw_text(10, 64, F_C16, C_HEAD, 0, dec->fm ? "Signal" : "Level");
    draw_rect(66, 51, 140, 14, C_DIM);
    draw_rect(66, 51, (int)(140 * level_q), 14, (md_rgb){42, 158, 75});
    draw_text(212, 64, F_C16, C_HEAD, 0, level_txt);
  }
  // etat du decodeur, coupe avant le compteur de droite
  if (g_status[0] || paused)
  {
    int sx = (level_q >= 0) ? 290 : 10;
    char st[128];
    fit_copy(st, sizeof st, F_C16, g_status[0] ? g_status : "Paused", 640 - text_width(F_C16, right) - 16 - sx);
    draw_text(sx, 64, F_C16, (paused || !g_status[0]) ? C_AMBER : C_GREEN, 0, st);
  }
  draw_rect(8, 72, 636, 1, C_GRID);
}

static void draw_list(void)
{
  int y = 96, k;
  if (g_nmsg == 0)
  {
    draw_text(10, 110, F_R20, C_HEAD, 0, child > 0 ? "Waiting for signals..." : "Receiver not running");
    return;
  }
  for (k = scroll_off; k < g_nmsg && y < 460; k++)
  {
    md_msg *m = &g_msg[(g_msghead - 1 - k + MSG_MAX * 2) % MSG_MAX];
    char hl[160], lines[4][160], ts[16];
    struct tm tm;
    int nl, i;
    localtime_r(&m->t, &tm);
    strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    snprintf(hl, sizeof hl, "%s   %s", ts, m->head);
    fit_copy(hl, sizeof hl, F_C16, hl, 630);
    draw_text(10, y, F_C16, m->col.r == C_WHITE.r && m->col.b == C_WHITE.b ? C_HEAD : m->col, 0, hl);
    y += 25;
    nl = wrap(m->body, F_R20, 630, lines, 3);
    for (i = 0; i < nl && y < 478; i++)
    {
      draw_text(10, y, F_R20, m->col, 0, lines[i]);
      y += 24;
    }
    y += 10;
    draw_rect(8, y - 18, 636, 1, C_GRID);
    y += 12;
  }
}

static void draw_stream(void)
{
  int rows = 15, first = g_stream_lines > rows ? g_stream_lines - rows : 0, i, y = 100;
  for (i = first; i < g_stream_lines; i++)
  {
    draw_text(10, y, F_R20, C_WHITE, 0, g_stream[i]);
    y += 25;
  }
}

static double range_km(void)
{
  static const double r[4][4] = {{50, 100, 200, 400}, {25, 50, 100, 200}, {500, 1000, 2000, 5000}, {10, 25, 50, 100}};
  int rset = (dec->objtype == O_AIRCRAFT) ? 0 : ((dec->objtype == O_STATION) ? 2 : ((dec->objtype == O_SHIP) ? 3 : 1));
  return r[rset][range_idx];
}

// Objets recents du type du decodeur, du plus proche au plus loin (puis sans position)
static int sorted_objs(int *idx, int max)
{
  time_t now = time(NULL);
  double key[OBJ_MAX];
  int n = 0, i, j;
  for (i = 0; i < g_nobj && n < max; i++)
  {
    md_obj *o = &g_obj[i];
    double km = 1e9, az;
    if ((o->type != dec->objtype) || (now - o->seen > obj_timeout(o->type))) continue;
    if (o->pos && isfinite(g_slat)) dist_az(o->lat, o->lon, &km, &az);
    else km = 1e9 + (now - o->seen);
    idx[n] = i;
    key[n++] = km;
  }
  for (i = 1; i < n; i++)
  {
    for (j = i; j > 0 && key[j] < key[j - 1]; j--)
    {
      double t = key[j]; key[j] = key[j - 1]; key[j - 1] = t;
      int u = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = u;
    }
  }
  return n;
}

// Rectangle occupe sur le radar (icone, point, etiquette, graduation)
typedef struct { float x0, y0, x1, y1; } md_box;

static bool box_free(md_box b, const md_box *used, int n)
{
  int k;
  for (k = 0; k < n; k++)
  {
    if ((b.x0 < used[k].x1) && (used[k].x0 < b.x1) && (b.y0 < used[k].y1) && (used[k].y0 < b.y1)) return false;
  }
  return true;
}

// Texte F_C16 de largeur w, ligne de base en y, aligne en x comme draw_text
// (0 a gauche, 1 centre, 2 a droite)
static md_box text_box(float x, float y, int w, int align)
{
  float x0 = (align == 2) ? x - w : ((align == 1) ? x - w / 2 : x);
  md_box b = {x0, y - 12, x0 + w, y + 4};
  return b;
}

static void radar_text(int x, int y, const char *s, md_box *used, int *nu)
{
  draw_text(x, y, F_C16, C_HEAD, 0, s);
  used[(*nu)++] = text_box(x, y, text_width(F_C16, s), 0);
}

static void draw_radar(void)
{
  const int cx = 520, cy = 272, R = 122;
  static md_box used[2 * OBJ_MAX + 4];
  static struct { md_obj *o; float x, y, h; double km, az; bool big; } pt[OBJ_MAX];
  double rk = range_km();
  int idx[OBJ_MAX], n, i, np = 0, nu = 0;
  char s[24];
  if (!isfinite(g_slat))
  {
    draw_text(cx, cy, F_C16, C_HEAD, 1, "Station position unknown");
    draw_text(cx, cy + 20, F_C16, C_HEAD, 1, "(Menu > Config)");
    return;
  }
  // cercles a 1/4, 1/2 et la portee entiere (valeurs rondes)
  draw_circle(cx, cy, R / 4.0f, C_SLATE, 0);
  draw_circle(cx, cy, R / 2.0f, C_SLATE, 0);
  draw_circle(cx, cy, R, C_SLATE, 0);
  draw_line(cx - R, cy, cx + R, cy, C_SLATE);
  draw_line(cx, cy - R, cx, cy + R, C_SLATE);
  radar_text(cx + 3, cy - R + 14, "N", used, &nu);
  snprintf(s, sizeof s, "%.0f", rk / 2);
  radar_text(cx + 4, cy + R / 2 + 16, s, used, &nu);
  snprintf(s, sizeof s, "%.0f km", rk);
  radar_text(cx + 4, cy + R - 4, s, used, &nu);
  used[nu++] = (md_box){cx - 4, cy - 4, cx + 4, cy + 4};
  // 1 : points et icones, du plus loin au plus proche (le plus proche dessus)
  n = sorted_objs(idx, OBJ_MAX);
  for (i = n - 1; i >= 0; i--)
  {
    md_obj *o = &g_obj[idx[i]];
    double km, az;
    float x, y, h = 4;
    md_rgb col;
    bool icon, plane, big = true;
    if (!o->pos) continue;
    dist_az(o->lat, o->lon, &km, &az);
    if (km > rk) continue;
    x = cx + km / rk * R * sin(az * M_PI / 180);
    y = cy - km / rk * R * cos(az * M_PI / 180);
    col = (isfinite(o->alt) && (o->type == O_AIRCRAFT) && (o->alt < 3000)) ? C_GREEN : C_WHITE;
    // station APRS : son icone (cap dessine dessous, il en depasse) ; avion
    // dont le cap est connu : sa silhouette orientee ; sinon un point
    icon = o->sym[0] && (o->type == O_APRS);
    plane = (o->type == O_AIRCRAFT) && isfinite(o->crs);
    if (!plane && isfinite(o->crs) && isfinite(o->spd) && (o->spd > 1))
    {
      float len = icon ? 20 : 12;
      draw_line(x, y, x + len * sin(o->crs * M_PI / 180), y - len * cos(o->crs * M_PI / 180), col);
    }
    if (plane)
    {
      draw_plane(x, y, 9, o->crs, col);
      h = 8;
    }
    else if (icon && draw_aprs_symbol((int)floorf(x), (int)floorf(y), o->sym))
    {
      h = MD_SYM / 2 - 2;                // les icones ont une marge transparente
    }
    else
    {
      draw_circle(x, y, 3.5f, col, 1);
      big = false;
    }
    used[nu++] = (md_box){x - h, y - h, x + h, y + h};
    pt[np].o = o; pt[np].x = x; pt[np].y = y; pt[np].h = h; pt[np].km = km; pt[np].az = az; pt[np].big = big;
    np++;
  }
  // 2 : etiquettes, du plus proche au plus loin. Places essayees : cote ouest
  // pour un point a l'ouest (sinon est), l'autre cote, dessous, dessus ; aucune
  // si toutes chevauchent une icone, une autre etiquette ou une graduation, ou
  // sortent du radar
  for (i = np - 1; i >= 0; i--)
  {
    const char *lab = pt[i].o->name[0] ? pt[i].o->name : pt[i].o->id;
    int tw = text_width(F_C16, lab), c;
    float x = pt[i].x, y = pt[i].y, h = pt[i].h;
    if (pt[i].km <= rk / 8) continue;     // pres du centre : trop serre
    for (c = 0; c < 4; c++)
    {
      float lx = x, ly;
      int align = 1;
      md_box b;
      if (c < 2)
      {
        bool left = (pt[i].az >= 180) == (c == 0);
        float dx = pt[i].big ? h + 5 : 7;
        lx = left ? x - dx : x + dx;
        ly = pt[i].big ? y + 6 : y + 16;   // a cote de l'icone ou de l'avion, sous le point
        align = left ? 2 : 0;
      }
      else
      {
        ly = (c == 2) ? y + h + 14 : y - h - 4;
      }
      b = text_box(lx, ly, tw, align);
      if ((b.x0 < cx - R - 4) || (b.x1 > 646) || (b.y0 < 80) || (b.y1 > MD_H) || !box_free(b, used, nu)) continue;
      used[nu++] = b;
      draw_text((int)lx, (int)ly, F_C16, C_HEAD, align, lab);
      break;
    }
  }
  draw_circle(cx, cy, 3, C_AMBER, 1);
}

static void draw_table(void)
{
  int idx[OBJ_MAX], n = sorted_objs(idx, OBJ_MAX), i, y = 120;
  char s[96];
  if (dec->objtype == O_AIRCRAFT)
  {
    draw_text(10, 96, F_C16, C_HEAD, 0, "ICAO"); draw_text(90, 96, F_C16, C_HEAD, 0, "Flight");
    draw_text(250, 96, F_C16, C_HEAD, 2, "Alt ft"); draw_text(296, 96, F_C16, C_HEAD, 2, "kt");
    draw_text(342, 96, F_C16, C_HEAD, 2, "Hdg"); draw_text(388, 96, F_C16, C_HEAD, 2, "km");
  }
  else if (dec->objtype == O_SHIP)
  {
    draw_text(10, 96, F_C16, C_HEAD, 0, "MMSI"); draw_text(116, 96, F_C16, C_HEAD, 0, "Name");
    draw_text(300, 96, F_C16, C_HEAD, 2, "kn"); draw_text(344, 96, F_C16, C_HEAD, 2, "Crs");
    draw_text(388, 96, F_C16, C_HEAD, 2, "km");
  }
  else if (dec->objtype == O_STATION)
  {
    draw_text(10, 96, F_C16, C_HEAD, 0, "Call"); draw_text(120, 96, F_C16, C_HEAD, 0, "Loc");
    draw_text(226, 96, F_C16, C_HEAD, 2, "dB"); draw_text(306, 96, F_C16, C_HEAD, 2, "km");
    draw_text(388, 96, F_C16, C_HEAD, 2, "Age");
  }
  else
  {
    draw_text(10, 96, F_C16, C_HEAD, 0, "Call"); draw_text(196, 96, F_C16, C_HEAD, 2, "km");
    draw_text(240, 96, F_C16, C_HEAD, 2, "Az"); draw_text(288, 96, F_C16, C_HEAD, 2, "Age");
    draw_text(300, 96, F_C16, C_HEAD, 0, "");
  }
  for (i = 0; i < n && y < 470; i++, y += 32)
  {
    md_obj *o = &g_obj[idx[i]];
    double km = NAN, az = NAN;
    if (o->pos && isfinite(g_slat)) dist_az(o->lat, o->lon, &km, &az);
    if (dec->objtype == O_AIRCRAFT)
    {
      draw_text(10, y, F_C18, C_WHITE, 0, o->id);
      draw_text(90, y, F_C18, C_WHITE, 0, o->name[0] ? o->name : "-");
      if (isfinite(o->alt))
      {
        int ft = (int)(o->alt / 0.3048 + 0.5);
        snprintf(s, sizeof s, ft >= 1000 ? "%d %03d" : "%d", ft >= 1000 ? ft / 1000 : ft, ft % 1000);
        draw_text(250, y, F_C18, (o->alt < 3000) ? C_GREEN : C_WHITE, 2, s);
      }
      if (isfinite(o->spd)) { snprintf(s, sizeof s, "%.0f", o->spd); draw_text(296, y, F_C18, C_WHITE, 2, s); }
      if (isfinite(o->crs)) { snprintf(s, sizeof s, "%03.0f", o->crs); draw_text(342, y, F_C18, C_WHITE, 2, s); }
    }
    else if (dec->objtype == O_SHIP)
    {
      draw_text(10, y, F_C18, C_WHITE, 0, o->id);
      fit_copy(s, sizeof s, F_C18, o->name[0] ? o->name : "-", 132);
      draw_text(116, y, F_C18, C_WHITE, 0, s);
      if (isfinite(o->spd)) { snprintf(s, sizeof s, "%.1f", o->spd); draw_text(300, y, F_C18, C_WHITE, 2, s); }
      if (isfinite(o->crs)) { snprintf(s, sizeof s, "%03.0f", o->crs); draw_text(344, y, F_C18, C_WHITE, 2, s); }
    }
    else if (dec->objtype == O_STATION)
    {
      draw_text(10, y, F_C18, C_WHITE, 0, o->id);
      draw_text(120, y, F_C18, C_WHITE, 0, o->name);
      if (isfinite(o->snr)) { snprintf(s, sizeof s, "%+.0f", o->snr); draw_text(226, y, F_C18, C_WHITE, 2, s); }
      if (isfinite(km)) { snprintf(s, sizeof s, "%.0f", km); draw_text(306, y, F_C18, C_WHITE, 2, s); }
      draw_text(388, y, F_C18, C_WHITE, 2, age_str(o->seen));
      continue;
    }
    else
    {
      fit_copy(s, sizeof s, F_C18, o->id, 150);
      draw_text(10, y, F_C18, C_WHITE, 0, s);
      if (isfinite(az)) { snprintf(s, sizeof s, "%03.0f", az); draw_text(240, y, F_C18, C_WHITE, 2, s); }
      draw_text(288, y, F_C18, C_WHITE, 2, age_str(o->seen));
      if (isfinite(km)) { snprintf(s, sizeof s, "%.0f", km); draw_text(196, y, F_C18, C_WHITE, 2, s); }
      fit_copy(s, sizeof s, F_C16, o->info, 100);
      draw_text(298, y, F_C16, C_HEAD, 0, s);
      continue;
    }
    if (isfinite(km)) { snprintf(s, sizeof s, "%.0f", km); draw_text(388, y, F_C18, C_WHITE, 2, s); }
  }
  draw_radar();
}

static void draw_sensors(void)
{
  int idx[OBJ_MAX], n = sorted_objs(idx, OBJ_MAX), i, y = 120;
  char s[120];
  bool rem = !strcmp(dec->key, "remotes");
  draw_text(10, 96, F_C16, C_HEAD, 0, rem ? "Remote" : "Sensor");
  draw_text(240, 96, F_C16, C_HEAD, 0, rem ? "Button, code" : "Values");
  draw_text(600, 96, F_C16, C_HEAD, 2, "Age");
  draw_text(640, 96, F_C16, C_HEAD, 2, "Nb");
  for (i = 0; i < n && y < 470; i++, y += 30)
  {
    md_obj *o = &g_obj[idx[i]];
    char id[80];
    snprintf(id, sizeof id, "%s %s", o->name, o->extra);
    fit_copy(s, sizeof s, F_C18, id, 225);
    draw_text(10, y, F_C18, C_WHITE, 0, s);
    fit_copy(s, sizeof s, F_C16, o->info, 300);
    draw_text(240, y, F_C16, C_GREEN, 0, s);
    draw_text(600, y, F_C18, C_WHITE, 2, age_str(o->seen));
    snprintf(s, sizeof s, "%ld", o->count);
    draw_text(640, y, F_C18, C_WHITE, 2, s);
  }
}

// ---------------------------------------------------------------- radio (RDS, DAB+)

#define RADIO_ROW 26
static float radio_row_mhz[24];          // frequence de chaque ligne (stations FM : toucher pour ecouter)
static char radio_row_id[24][24];        // service de chaque ligne (DAB : toucher pour l'ecouter sur le Pi)
static int radio_row_y0, radio_nrows;
static int radio_off, radio_total, radio_more_y;   // tableau trop long : ligne "plus" a toucher
static char clock_note[120];             // resultat de Set Clock, affiche 15 s
static time_t clock_note_t;

static int svc_by_name(const void *a, const void *b)
{
  return strcasecmp(g_obj[*(const int *)a].name, g_obj[*(const int *)b].name);
}

static int svc_by_mhz(const void *a, const void *b)
{
  float x = g_obj[*(const int *)a].mhz, y = g_obj[*(const int *)b].mhz;
  return (x > y) - (x < y);
}

// Heure diffusee (a la minute) et ecart de l'horloge du Pi : vert sous 1 s,
// ambre sous 5 s, rouge au-dela
static void draw_radio_time(int y)
{
  char s[160], t[40], z[16];
  time_t now = time(NULL), lt;
  struct tm tm;
  bool ntp = ntp_synced();
  if (!g_radio.got)
  {
    snprintf(s, sizeof s, "Time : not received yet%s", ntp ? "     Pi clock : NTP synchronized" : "");
    draw_text(10, y, F_C18, C_HEAD, 0, s);
  }
  else
  {
    double d = g_radio.diff;
    lt = g_radio.utc + g_radio.lto * 60;
    gmtime_r(&lt, &tm);
    strftime(t, sizeof t, "%d/%m/%Y %H:%M", &tm);
    if (g_radio.lto % 60) snprintf(z, sizeof z, "UTC%+.1f", g_radio.lto / 60.0);
    else snprintf(z, sizeof z, "UTC%+d", g_radio.lto / 60);
    snprintf(s, sizeof s, "%s %s %s", dec->parser == P_DAB ? "DAB" : "RDS", t, z);
    draw_text(10, y, F_C18, C_WHITE, 0, s);
    snprintf(s, sizeof s, "Pi clock %+.1f s%s", d, ntp ? "  (NTP)" : "");
    draw_text(300, y, F_C18, (fabs(d) < 1) ? C_GREEN : ((fabs(d) < 5) ? C_AMBER : C_RED), 0, s);
    if (now - g_radio.got > 90)
    {
      snprintf(s, sizeof s, "%s ago", age_str(g_radio.got));
      draw_text(638, y, F_C16, C_HEAD, 2, s);
    }
  }
  if (clock_note[0] && (now - clock_note_t < 15))
  {
    draw_text(10, y + 20, F_C16, C_AMBER, 0, clock_note);
  }
}

// Station ou multiplex en cours, heure, puis tableau : services du multiplex
// (DAB, par nom) ou stations FM (par frequence, la station ecoutee en vert)
static void draw_radio(void)
{
  int idx[OBJ_MAX], n = 0, i, y, x;
  char s[200], lines[2][160];
  time_t now = time(NULL);
  bool dab = (dec->parser == P_DAB);
  double fq = atof(dec_freq(dec));
  for (i = 0; i < g_nobj; i++)
  {
    if ((g_obj[i].type == O_SERVICE) && (now - g_obj[i].seen <= obj_timeout(O_SERVICE))) idx[n++] = i;
  }
  qsort(idx, n, sizeof idx[0], dab ? svc_by_name : svc_by_mhz);
  if (g_radio.name[0] || g_radio.id[0])
  {
    fit_copy(s, sizeof s, F_B24, g_radio.name[0] ? g_radio.name : "?", 380);
    draw_text(10, 106, F_B24, C_WHITE, 0, s);
    x = 10 + text_width(F_B24, s) + 16;
    snprintf(s, sizeof s, "%s %s", dab ? "ID" : "PI", g_radio.id);
    draw_text(x, 106, F_C18, C_HEAD, 0, s);
  }
  else
  {
    draw_text(10, 106, F_R20, C_HEAD, 0,
              scanning ? "Scanning..." : (dab ? "Waiting for a DAB multiplex..." : "Waiting for RDS data..."));
  }
  if (dab)
  {
    fit_copy(s, sizeof s, F_C16, g_radio.extra, 628);
  }
  else
  {
    snprintf(s, sizeof s, "%s%s%s%s", g_radio.pty, (g_radio.pty[0] && (g_radio.tp || g_radio.ta)) ? "   " : "",
             g_radio.tp ? "TP " : "", g_radio.ta ? "TA" : "");
  }
  draw_text(10, 130, F_C16, C_HEAD, 0, s);
  if (!dab && g_radio.extra[0])
  {
    fit_copy(s, sizeof s, F_C16, g_radio.extra, 628);
    if (strlen(s) < strlen(g_radio.extra) && strrchr(s, ' ')) *strrchr(s, ' ') = 0;   // pas de nombre coupe
    draw_text(10, 150, F_C16, C_HEAD, 0, s);
  }
  draw_radio_time(dab ? 156 : 176);
  y = dab ? 196 : 216;
  if (!dab)
  {
    int nl = wrap(g_radio.text, F_R20, 625, lines, 2), k;
    for (k = 0; k < nl; k++, y += 24) draw_text(10, y, F_R20, C_WHITE, 0, lines[k]);
    if (!nl) y += 24;
    y += 6;
  }
  if (dab)
  {
    draw_text(10, y, F_C16, C_HEAD, 0, "Service"); draw_text(200, y, F_C16, C_HEAD, 0, "Type");
    draw_text(290, y, F_C16, C_HEAD, 0, "PTY"); draw_text(400, y, F_C16, C_HEAD, 0, "Now playing");
  }
  else
  {
    draw_text(62, y, F_C16, C_HEAD, 2, "MHz"); draw_text(80, y, F_C16, C_HEAD, 0, "Station");
    draw_text(200, y, F_C16, C_HEAD, 0, "PI"); draw_text(270, y, F_C16, C_HEAD, 0, "PTY");
    draw_text(400, y, F_C16, C_HEAD, 0, "RadioText");
  }
  y += 24;
  radio_row_y0 = y - 18;
  radio_nrows = 0;
  radio_total = n;
  {
    int fit = (474 - y) / RADIO_ROW + 1;
    if (n > fit) fit--;                  // la derniere ligne dit qu'il y en a d'autres
    if (fit > 24) fit = 24;
    if ((radio_off >= n) || (n <= fit)) radio_off = 0;
    i = radio_off;
    for (; (i < n) && (radio_nrows < fit); i++, y += RADIO_ROW)
    {
    md_obj *o = &g_obj[idx[i]];
    bool play = dab && dab_playing[0] && !strcmp(o->id, dab_playing);
    md_rgb c = ((!dab && (fabs(o->mhz - fq) < 0.05)) || play) ? C_GREEN : C_WHITE;
    snprintf(radio_row_id[radio_nrows], sizeof radio_row_id[0], "%s", o->id);
    radio_row_mhz[radio_nrows++] = dab ? 0 : o->mhz;
    if (dab)
    {
      fit_copy(s, sizeof s, F_C18, o->name[0] ? o->name : o->id, 185);
      draw_text(10, y, F_C18, c, 0, s);
      fit_copy(s, sizeof s, F_C16, o->kind, 85);
      draw_text(200, y, F_C16, C_HEAD, 0, s);
      fit_copy(s, sizeof s, F_C16, o->pty, 105);
    }
    else
    {
      snprintf(s, sizeof s, "%.1f", o->mhz);
      draw_text(62, y, F_C18, c, 2, s);
      fit_copy(s, sizeof s, F_C18, o->name[0] ? o->name : "-", 115);
      draw_text(80, y, F_C18, c, 0, s);
      draw_text(200, y, F_C16, C_HEAD, 0, o->kind);
      fit_copy(s, sizeof s, F_C16, o->pty, 125);
    }
    draw_text(dab ? 290 : 270, y, F_C16, C_HEAD, 0, s);
    if (play)
    {
      char t[110];
      snprintf(t, sizeof t, "> %s", o->info[0] ? o->info : "playing on the Pi");
      fit_copy(s, sizeof s, F_C16, t, 238);
    }
    else
    {
      fit_copy(s, sizeof s, F_C16, o->info, 238);
    }
    draw_text(400, y, F_C16, play ? C_GREEN : C_WHITE, 0, s);
    }
  }
  radio_more_y = 0;
  if (radio_nrows < n)
  {
    snprintf(s, sizeof s, "%d-%d of %d   Touch here for more%s", radio_off + 1, radio_off + radio_nrows, n,
             scanning ? "" : (dab ? "   (touch a service to play it)" : "   (touch a station to tune to it)"));
    draw_text(10, y, F_C16, C_AMBER, 0, s);
    radio_more_y = y - 18;
  }
  else if ((y < 474) && !scanning && (dab ? (n > 0) : (n > 1)))
  {
    draw_text(10, y, F_C16, C_HEAD, 0, dab ? "Touch a service to play it on the Pi (again : stop)"
                                           : "Touch a station to tune to it");
  }
}

// Stations FM du tableau : toucher une ligne y regle le recepteur
static bool radio_tap(int x, int y)
{
  int r;
  char f[16];
  if ((screen != S_RUN) || (dec->view != V_RADIO) || (x >= 640) || (y < radio_row_y0))
  {
    return false;
  }
  if (radio_more_y && (y >= radio_more_y))   // ligne "plus" : lignes suivantes, puis retour au debut
  {
    radio_off += radio_nrows;
    if (radio_off >= radio_total) radio_off = 0;
    g_dirty = true;
    return true;
  }
  if (scanning)
  {
    return false;
  }
  r = (y - radio_row_y0) / RADIO_ROW;
  if ((dec->parser == P_DAB) && (r < radio_nrows))
  {
    // service DAB : joue sur le Pi (md_radio.py), ou arrete s'il l'etait deja
    FILE *f = fopen(MD_SHM "/dab_play", "w");
    if (!strcmp(radio_row_id[r], dab_playing)) dab_playing[0] = 0;
    else snprintf(dab_playing, sizeof dab_playing, "%s", radio_row_id[r]);
    if (f)
    {
      fprintf(f, "%s\n", dab_playing);
      fclose(f);
    }
    g_dirty = true;
    return true;
  }
  if (dec->parser != P_RDS)
  {
    return false;
  }
  if ((r >= radio_nrows) || (radio_row_mhz[r] <= 0))
  {
    return false;
  }
  if (fabs(radio_row_mhz[r] - atof(dec_freq(dec))) >= 0.05)
  {
    snprintf(f, sizeof f, "%.1f", radio_row_mhz[r]);
    conf_set(ckey("freq"), f);
    memset(&g_radio, 0, sizeof g_radio);
    redraw_static = true;                // titre : frequence
    start_decoder(false);
  }
  g_dirty = true;
  return true;
}

// Met l'horloge du Pi a l'heure diffusee (sudo date), sauf si NTP la tient
static void set_clock(void)
{
  char cmd[80];
  struct timeval tv;
  const char *src = (dec->parser == P_DAB) ? "DAB" : "RDS";
  clock_note_t = time(NULL);
  if (!g_radio.got || (time(NULL) - g_radio.got > 600))
  {
    snprintf(clock_note, sizeof clock_note, "Set clock : no %s time received in the last 10 minutes", src);
    return;
  }
  if (ntp_synced())
  {
    snprintf(clock_note, sizeof clock_note, "Set clock : the Pi clock is NTP synchronized, not changed");
    return;
  }
  gettimeofday(&tv, NULL);
  snprintf(cmd, sizeof cmd, "sudo date -u -s @%.2f > /dev/null 2>&1", tv.tv_sec + tv.tv_usec / 1e6 - g_radio.diff);
  if (system(cmd) == 0)
  {
    snprintf(clock_note, sizeof clock_note, "Set clock : Pi clock moved by %+.1f s to the %s time", -g_radio.diff, src);
    msg_add(C_GREEN, "Set clock", clock_note + 12);
    g_radio.got -= (time_t)g_radio.diff;
    g_radio.diff = 0;
    clock_note_t = time(NULL);
  }
  else
  {
    snprintf(clock_note, sizeof clock_note, "Set clock : sudo date failed");
  }
}

static void draw_sonde(void)
{
  md_obj *o = NULL;
  char s[64];
  int i, y;
  double km = NAN, az = NAN, amax = 1000;
  const int gx = 350, gy = 90, gw = 290, gh = 220, mx = 350, my = 322, mw = 290, mh = 150;
  for (i = 0; i < g_nobj; i++)
  {
    if (g_obj[i].type != O_SONDE) continue;
    if (!strcmp(g_obj[i].id, g_track_id)) { o = &g_obj[i]; break; }
    if (!o || g_obj[i].seen > o->seen) o = &g_obj[i];
  }
  if (!o)
  {
    draw_text(10, 110, F_R20, C_HEAD, 0, scanning ? "Scanning for radiosondes..." : "Waiting for a radiosonde...");
    return;
  }
  if (o->pos && isfinite(g_slat)) dist_az(o->lat, o->lon, &km, &az);
  {
    // numero de serie puis type (omis s'il est dans le numero : "M10-..."),
    // sans deborder sur le profil (x 350)
    int f = (text_width(F_B24, o->id) <= 330) ? F_B24 : F_C20, x;
    fit_copy(s, sizeof s, f, o->id, 330);
    draw_text(10, 106, f, C_WHITE, 0, s);
    x = 10 + text_width(f, s) + 14;
    if ((x < 300) && o->name[0] && !strstr(o->id, o->name))
    {
      fit_copy(s, sizeof s, F_C18, o->name, 338 - x);
      draw_text(x, 106, F_C18, C_HEAD, 0, s);
    }
  }
  {
    const char *lab[10] = {"Altitude", "Climb", "Latitude", "Longitude", "Distance", "Azimuth", "Temp",
                           "Humidity", "Battery", "Burst kill"};
    char val[10][32];
    snprintf(val[0], 32, isfinite(o->alt) ? "%.0f m" : "-", o->alt);
    snprintf(val[1], 32, isfinite(o->vz) ? "%+.1f m/s" : "-", o->vz);
    snprintf(val[2], 32, o->pos ? "%.5f" : "-", o->lat);
    snprintf(val[3], 32, o->pos ? "%.5f" : "-", o->lon);
    snprintf(val[4], 32, isfinite(km) ? "%.1f km" : "-", km);
    snprintf(val[5], 32, isfinite(az) ? "%.0f deg" : "-", az);
    snprintf(val[6], 32, isfinite(o->temp) ? "%.1f C" : "-", o->temp);
    if (isfinite(o->hum)) snprintf(val[7], 32, "%s%.0f %%", o->hum_approx ? "~" : "", o->hum);   // ~ : approchee (M10)
    else snprintf(val[7], 32, "-");
    snprintf(val[8], 32, isfinite(o->batt) ? "%.1f V" : "-", o->batt);
    if (o->burst >= 0) snprintf(val[9], 32, "%02d:%02d:%02d", o->burst / 3600, o->burst / 60 % 60, o->burst % 60);
    else snprintf(val[9], 32, "-");
    for (i = 0, y = 150; i < 10; i += 2, y += 62)
    {
      draw_text(10, y, F_C16, C_HEAD, 0, lab[i]);
      draw_text(10, y + 26, F_B22, i == 0 ? C_GREEN : C_WHITE, 0, val[i]);
      draw_text(180, y, F_C16, C_HEAD, 0, lab[i + 1]);
      draw_text(180, y + 26, F_B22, C_WHITE, 0, val[i + 1]);
    }
  }
  // profil d'altitude de la sonde suivie
  draw_rect(gx, gy, gw, gh, C_PANEL);
  draw_text(gx + 4, gy + 18, F_C16, C_HEAD, 0, "Altitude (m)");
  for (i = 0; i < g_ntrack; i++) if (g_track[i].alt > amax) amax = g_track[i].alt;
  amax = ceil(amax * 1.15 / 5000) * 5000;
  for (i = 1; i < 4; i++) draw_rect(gx, gy + gh * i / 4, gw, 1, C_GRID);
  snprintf(s, sizeof s, "%.0f", amax * 3 / 4);
  draw_text(gx + 4, gy + gh / 4 - 3, F_C16, C_HEAD, 0, s);
  if (g_ntrack > 1)
  {
    time_t t0 = g_track[0].t, t1 = g_track[g_ntrack - 1].t;
    double span = (t1 > t0) ? (t1 - t0) : 1;
    float px = 0, py = 0;
    for (i = 0; i < g_ntrack; i++)
    {
      float x = gx + 8 + (g_track[i].t - t0) / span * (gw - 16);
      float yy = gy + gh - 8 - g_track[i].alt / amax * (gh - 16);
      if (i) draw_line(px, py, x, yy, C_GREEN);
      px = x; py = yy;
    }
    draw_circle(px, py, 4, C_WHITE, 1);
    snprintf(s, sizeof s, "%.0f min", span / 60);
    draw_text(gx + gw - 6, gy + gh - 10, F_C16, C_HEAD, 2, s);
  }
  // trajectoire et station (echelle automatique)
  draw_rect(mx, my, mw, mh, C_PANEL);
  draw_text(mx + 4, my + 18, F_C16, C_HEAD, 0, "Track");
  if (g_ntrack > 0)
  {
    double la0 = g_track[0].lat, la1 = la0, lo0 = g_track[0].lon, lo1 = lo0, sc, cl;
    float px = 0, py = 0;
    for (i = 0; i < g_ntrack; i++)
    {
      la0 = fmin(la0, g_track[i].lat); la1 = fmax(la1, g_track[i].lat);
      lo0 = fmin(lo0, g_track[i].lon); lo1 = fmax(lo1, g_track[i].lon);
    }
    if (isfinite(g_slat))
    {
      la0 = fmin(la0, g_slat); la1 = fmax(la1, g_slat); lo0 = fmin(lo0, g_slon); lo1 = fmax(lo1, g_slon);
    }
    cl = cos((la0 + la1) / 2 * M_PI / 180);
    // marges : 20 pixels, 44 en haut (l'icone de la sonde depasse de 43 pixels
    // au-dessus de sa position)
    sc = fmin((mw - 40) / fmax((lo1 - lo0) * cl, 1e-4), (mh - 64) / fmax(la1 - la0, 1e-4));
#define MXP(lo) (mx + mw / 2 + ((lo) - (lo0 + lo1) / 2) * cl * sc)
#define MYP(la) (my + mh / 2 + 12 - ((la) - (la0 + la1) / 2) * sc)
    for (i = 0; i < g_ntrack; i++)
    {
      float x = MXP(g_track[i].lon), yy = MYP(g_track[i].lat);
      if (i) draw_line(px, py, x, yy, C_GREEN);
      px = x; py = yy;
    }
    if (isfinite(g_slat))
    {
      draw_circle(MXP(g_slon), MYP(g_slat), 4, C_AMBER, 1);
      draw_text(MXP(g_slon) - 8, MYP(g_slat) - 8, F_C16, C_HEAD, 2, "Station");
    }
    draw_sonde_icon(px, py);              // position de la sonde, par-dessus la station
    {
      double km10 = 10.0 / 111.0 * sc;     // 10 km en pixels
      snprintf(s, sizeof s, "%s", km10 > 120 ? "1 km" : (km10 > 25 ? "10 km" : "100 km"));
      double L = km10 > 120 ? km10 / 10 : (km10 > 25 ? km10 : km10 * 10);
      draw_rect(mx + mw - 6 - (int)L, my + mh - 30, (int)L, 2, C_HEAD);
      draw_text(mx + mw - 6, my + mh - 8, F_C16, C_HEAD, 2, s);
    }
#undef MXP
#undef MYP
  }
}

static void draw_logview(void)
{
  int y = 96, i;
  if (log_n == 0)
  {
    draw_text(10, 110, F_R20, C_HEAD, 0, "No log for this decoder yet");
    return;
  }
  for (i = log_n - 1 - log_off; i >= 0 && y < 470; i--)
  {
    char *p1 = strstr(log_lines[i], " | "), *p2 = p1 ? strstr(p1 + 3, " | ") : NULL;
    char hl[160], lines[3][160];
    int nl, k;
    if (!p2) continue;
    snprintf(hl, sizeof hl, "%.*s   %.*s", (int)(p1 - log_lines[i]), log_lines[i], (int)(p2 - p1 - 3), p1 + 3);
    fit_copy(hl, sizeof hl, F_C16, hl, 630);
    draw_text(10, y, F_C16, C_HEAD, 0, hl);
    y += 24;
    nl = wrap(p2 + 3, F_R20, 630, lines, 2);
    for (k = 0; k < nl && y < 478; k++, y += 24) draw_text(10, y, F_R20, C_WHITE, 0, lines[k]);
    y += 10;
  }
}

static void draw_keypad_value(void)
{
  draw_rect(150, 52, 560, 64, C_PANEL);
  draw_rect(150, 52, 560, 2, C_SLATE);
  draw_text(170, 98, F_B24, C_GREEN, 0, kp_value[0] ? kp_value : " ");
}

static void draw_config_info(void)
{
  char s[160];
  snprintf(s, sizeof s, "Web map : http://%s/multidec/", myip[0] ? myip : "<Pi address>");
  draw_text(180, 410, F_C18, C_WHITE, 0, s);
  if (isfinite(g_slat)) snprintf(s, sizeof s, "Station : %.4f  %.4f", g_slat, g_slon);
  else snprintf(s, sizeof s, "Station position unknown");
  draw_text(180, 440, F_C18, C_HEAD, 0, s);
  snprintf(s, sizeof s, "Logs : %s", MD_LOGDIR);
  draw_text(180, 466, F_C16, C_HEAD, 0, s);
}

static void render(void)
{
  if (redraw_static)
  {
    build_static();
  }
  memcpy(fbp, chrome, MD_W * MD_H * 4);
  switch (screen)
  {
  case S_RUN:
    draw_status();
    if (dec->view == V_STREAM) draw_stream();
    else if ((dec->view == V_OBJ) && show_table) draw_table();
    else if ((dec->view == V_SENSOR) && show_table) draw_sensors();
    else if ((dec->view == V_SONDE) && show_table) draw_sonde();
    else if (dec->view == V_RADIO) draw_radio();
    else draw_list();
    break;
  case S_LOG: draw_logview(); break;
  case S_KEYPAD: draw_keypad_value(); break;
  case S_CONFIG: draw_config_info(); break;
  default: break;
  }
  draw_present();
  g_dirty = false;
}

// ---------------------------------------------------------------- actions

static void load_log(void)
{
  char best[300] = "", path[300], ln[200];
  DIR *dd = opendir(MD_LOGDIR);
  struct dirent *e;
  FILE *f;
  int kl = strlen(dec->key);
  log_n = log_off = 0;
  if (!dd) return;
  while ((e = readdir(dd)))
  {
    if (!strncmp(e->d_name, dec->key, kl) && (e->d_name[kl] == '_') && (strcmp(e->d_name, best) > 0))
    {
      snprintf(best, sizeof best, "%s", e->d_name);
    }
  }
  closedir(dd);
  if (!best[0]) return;
  snprintf(path, sizeof path, "%s/%s", MD_LOGDIR, best);
  f = fopen(path, "r");
  if (!f) return;
  while (fgets(ln, sizeof ln, f))
  {
    ln[strcspn(ln, "\n")] = 0;
    if (log_n == 300)
    {
      memmove(log_lines[0], log_lines[1], sizeof log_lines[0] * 299);
      log_n--;
    }
    snprintf(log_lines[log_n++], sizeof log_lines[0], "%s", ln);
  }
  fclose(f);
}

static void keypad(const char *target, const char *title, const char *init)
{
  snprintf(kp_target, sizeof kp_target, "%s", target);
  snprintf(kp_title, sizeof kp_title, "%s", title);
  snprintf(kp_value, sizeof kp_value, "%s", strcmp(init, "auto") ? init : "");
  kp_fresh = true;
  kp_return = screen;
  screen = S_KEYPAD;
}

static void keypad_done(bool ok)
{
  screen = kp_return;
  if (!ok) return;
  if (strspn(kp_value, "0123456789.-") != strlen(kp_value))
  {
    return;                              // ne devrait pas arriver : chiffres seulement
  }
  if (!strcmp(kp_target, "freq") && (atof(kp_value) > 0.1) && (atof(kp_value) < 2000))
  {
    conf_set(ckey("freq"), kp_value);
  }
  else if (!strcmp(kp_target, "gain"))
  {
    conf_set(ckey("gain"), (kp_value[0] && atof(kp_value) >= 0 && atof(kp_value) <= 50) ? kp_value : "auto");
  }
  else if (!strcmp(kp_target, "ppm") && (fabs(atof(kp_value)) <= 200))
  {
    char v[16];
    snprintf(v, sizeof v, "%d", atoi(kp_value));
    conf_set("ppm", v);
  }
  else if (!strcmp(kp_target, "lat") && (fabs(atof(kp_value)) <= 90) && kp_value[0])
  {
    conf_set("station_lat", kp_value);
  }
  else if (!strcmp(kp_target, "lon") && (fabs(atof(kp_value)) <= 180) && kp_value[0])
  {
    conf_set("station_lon", kp_value);
  }
  station_update();
}

static void leave(int code)
{
  stop_decoder();
  draw_clear();
  draw_present();
  exit(code);
}

static void receive(void)
{
  data_clear();
  paused = false;
  scroll_off = 0;
  scanning = false;
  show_table = (dec->parser == P_ADSB) || (dec->parser == P_AIS) || (dec->view == V_SENSOR) || (dec->view == V_SONDE);
  range_idx = ((dec->objtype == O_SHIP) || (dec->objtype == O_APRS)) ? 2 : 1;
  screen = S_RUN;
  start_decoder(false);
}

static void touch(int x, int y)
{
  int id = hit(x, y);
  if ((id < 0) && radio_tap(x, y))
  {
    return;
  }
  if (id < 0)
  {
    // en pause, toucher la liste la fait defiler (haut : recents, bas : anciens)
    if ((screen == S_RUN) && paused && (x < 640) && (y > 80))
    {
      scroll_off += (y > 280) ? 3 : -3;
      if (scroll_off > g_nmsg - 1) scroll_off = g_nmsg - 1;
      if (scroll_off < 0) scroll_off = 0;
      g_dirty = true;
    }
    return;
  }
  redraw_static = true;
  switch (screen)
  {
  case S_MENU:
    if (id < B_RECEIVE && id < NDECODERS)
    {
      dec = &DECODERS[id];
      conf_set("decoder", dec->key);
    }
    else if (id == B_MPAGE) menu_page++;
    else if (id == B_RECEIVE) receive();
    else if ((id == B_FREQ) && !dec->fixed) keypad("freq", "Frequency (MHz)", dec_freq(dec));
    else if (id == B_GAIN) keypad("gain", "Gain (dB, 0 - 49.6, or Auto)", dec_gain(dec));
    else if (id == B_PPM) keypad("ppm", "Frequency correction (ppm)", conf_get("ppm", "0"));
    else if ((id == B_OPTIONS) && dec->opt[0].key) screen = S_OPTIONS;
    else if (id == B_LOG) { load_log(); screen = S_LOG; }
    else if (id == B_DATV) leave(205);
    else if (id == B_EXIT) leave(129);
    else if (id == B_CONFIG) { find_ip(); screen = S_CONFIG; }
    break;
  case S_OPTIONS:
    if ((id >= B_OPT) && (id < B_OPT + 50))
    {
      int r = (id - B_OPT) / 10, v = (id - B_OPT) % 10;
      conf_set(ckey(dec->opt[r].key), dec->opt[r].vals[v]);
    }
    else if ((id == B_PRESET) || (id == B_PRESET + 1))
    {
      char p[128], *t, *save = NULL;
      snprintf(p, sizeof p, "%s", dec->preset[id - B_PRESET][1]);
      for (t = strtok_r(p, ",", &save); t; t = strtok_r(NULL, ",", &save))
      {
        char *eq = strchr(t, '=');
        if (eq) { *eq = 0; conf_set(ckey(t), eq + 1); }
      }
    }
    else if (id == B_EXIT) leave(129);
    else if (id == B_BACK) screen = S_MENU;
    break;
  case S_KEYPAD:
    if ((id >= B_KEY) && (id < B_KEY + 128))          // caracteres ASCII seulement
    {
      char c[2] = {(char)(id - B_KEY), 0};
      if (kp_fresh) kp_value[0] = 0;                  // la premiere touche remplace la valeur
      if (strlen(kp_value) < 12) strcat(kp_value, c);
    }
    else if (id == B_KDEL && kp_value[0]) kp_value[strlen(kp_value) - 1] = 0;
    else if (id == B_KCLR) kp_value[0] = 0;
    else if (id == B_KAUTO) { kp_value[0] = 0; keypad_done(true); }
    else if (id == B_KOK) keypad_done(true);
    else if (id == B_KCANCEL) keypad_done(false);
    kp_fresh = false;
    break;
  case S_CONFIG:
    if (id == B_POSLOC) conf_set("station", "locator");
    else if (id == B_POSMAN) conf_set("station", "manual");
    else if ((id == B_LAT) && !strcmp(conf_get("station", "locator"), "manual"))
      keypad("lat", "Station latitude (deg, North +)", conf_get("station_lat", ""));
    else if ((id == B_LON) && !strcmp(conf_get("station", "locator"), "manual"))
      keypad("lon", "Station longitude (deg, East +)", conf_get("station_lon", ""));
    else if (id == B_HFV4) conf_set("hf_input", "v4");
    else if (id == B_HFDIRECT) conf_set("hf_input", "direct");
    else if (id == B_LOGONOFF) conf_set("log", strcmp(conf_get("log", "on"), "off") ? "off" : "on");
    else if (id == B_EXIT) leave(129);
    else if (id == B_BACK) screen = S_MENU;
    station_update();
    break;
  case S_RUN:
    if (id == B_PAUSE) { paused = !paused; scroll_off = 0; }
    else if ((id == B_ACTION) && (dec->view == V_RADIO)) set_clock();
    else if ((id == B_VIEW) && (dec->view == V_RADIO)) { if (scanning) start_decoder(false); else start_decoder(true); }
    else if (id == B_ACTION)
    {
      if ((dec->view == V_OBJ) && show_table) range_idx = (range_idx + 1) % 4;
      else if (dec->view == V_SONDE) { if (scanning) start_decoder(false); else start_decoder(true); }
      else { data_clear(); scroll_off = 0; }
    }
    else if (id == B_VIEW) show_table = !show_table;
    else if (id == B_AUDIO) { conf_set("audio_mute", audio_muted() ? "off" : "on"); apply_mute(); }
    else if (id == B_MENU) { stop_decoder(); scanning = false; screen = S_MENU; menu_page = -1; }
    else if (id == B_EXIT) leave(129);
    break;
  case S_LOG:
    if (id == B_OLDER) log_off = (log_off + 8 < log_n) ? log_off + 8 : log_off;
    else if (id == B_NEWER) log_off = (log_off >= 8) ? log_off - 8 : 0;
    else if (id == B_BACK) screen = S_MENU;
    break;
  }
  g_dirty = true;
}

static void on_signal(int sig)
{
  (void)sig;
  if (child > 0) kill(-child, SIGTERM);
  _exit(129);
}

int main(void)
{
  bool web;
  int tfd;
  long long last_poll = 0, last_export = 0;
  time_t last_sec = 0;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);
  signal(SIGHUP, on_signal);

  conf_load();
  dec = dec_find(conf_get("decoder", "pocsag"));
  station_update();
  mkdir(MD_SHM, 0755);
  if (draw_init() != 0)
  {
    fprintf(stderr, "multidec : framebuffer indisponible\n");
  }
  chrome = malloc(MD_W * MD_H * 4);
  tfd = touch_init(&web);
  stream_clear();

  for (;;)
  {
    struct pollfd pf[2];
    int np = 0;
    struct timeval now;
    pf[np].fd = tfd; pf[np].events = POLLIN; np++;
    if (child_fd >= 0) { pf[np].fd = child_fd; pf[np].events = POLLIN; np++; }
    if (g_dirty || redraw_static)
    {
      render();
    }
    poll(pf, np, 250);
    if (pf[0].revents & POLLIN)
    {
      int ev[2];
      if (read(tfd, ev, sizeof ev) == sizeof ev) touch(ev[0], ev[1]);
    }
    if ((np > 1) && (pf[1].revents & (POLLIN | POLLHUP)))
    {
      read_child();
    }
    gettimeofday(&now, NULL);
    if (now_ms() - last_poll >= 250)
    {
      last_poll = now_ms();
      if (screen == S_RUN)
      {
        float q = level_q;
        if (!paused) parse_poll(dec);
        read_level();
        if (fabsf(q - level_q) > 0.01f) g_dirty = true;
      }
    }
    if (now.tv_sec != last_sec)
    {
      last_sec = now.tv_sec;
      if (screen == S_RUN) g_dirty = true;            // horloge, ages
    }
    if (now_ms() - last_export >= 2000)
    {
      last_export = now_ms();
      export_state();
    }
  }
  return 0;
}
