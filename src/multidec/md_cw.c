// md_cw.c : decodeur de morse (CW) du decodeur multiple, a partir de l'audio
// BLU (16 bits signes, mono, stdin). Remplace le MORSE_CW de multimon-ng, qui
// veut une tonalite, un seuil et une vitesse fixes.
//
//  - tonalite : banc de filtres de Goertzel de 300 a 1500 Hz (pas de 40 Hz,
//    blocs de ~12 ms, fenetre de Hann) ; la raie la plus forte est suivie,
//    avec hysteresis (une autre raie doit la depasser de 6 dB pendant 0,25 s) ;
//  - manipulation : enveloppe de la raie suivie (et de ses voisines : derive),
//    seuil adaptatif entre le plancher (bruit) et la crete (signal) avec
//    hysteresis, 6 dB de rapport signal/bruit au moins, anti-rebond d'un bloc ;
//  - vitesse : les 32 dernieres marques sont separees en deux groupes
//    (k-moyennes : points, traits) ; un seul groupe (que des E ou des T) est
//    departage par les silences entre elements, longs d'un point. Le biais de
//    mesure (marques allongees, silences raccourcis par le lissage) est estime
//    et retire. Seuils standards : trait > 2 points, lettre > 2 points de
//    silence, mot > 5 points. 4 a 60 mots/min ;
//  - sortie : le texte (stdout, sans tampon) ; une ligne "### ..." donne
//    l'etat (tonalite, vitesse, rapport signal/bruit), lue par multidec.
//    Signes inconnus : '*' ; prosignes : <AR> <SK> <BT> <KN> <AS> <HH>.
//
// Usage : md_cw -r <frequence>          MD_CW_DEBUG=1 : durees mesurees sur stderr

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define F_LO 300
#define F_HI 1500
#define F_STEP 40
#define NBIN ((F_HI - F_LO) / F_STEP + 1)
#define BLK_MS 11.6f                      // fenetre : 256 echantillons a 22050 Hz
#define HOP_MS (BLK_MS / 2)               // pas : fenetres a recouvrement de moitie
#define UNIT_MIN 20.0f                    // 60 mots/min
#define UNIT_MAX 300.0f                   // 4 mots/min
#define HIST 32                           // marques et silences gardes pour la vitesse

static const struct { const char *code; const char *txt; } MORSE[] = {
  {".-", "A"}, {"-...", "B"}, {"-.-.", "C"}, {"-..", "D"}, {".", "E"}, {"..-.", "F"}, {"--.", "G"},
  {"....", "H"}, {"..", "I"}, {".---", "J"}, {"-.-", "K"}, {".-..", "L"}, {"--", "M"}, {"-.", "N"},
  {"---", "O"}, {".--.", "P"}, {"--.-", "Q"}, {".-.", "R"}, {"...", "S"}, {"-", "T"}, {"..-", "U"},
  {"...-", "V"}, {".--", "W"}, {"-..-", "X"}, {"-.--", "Y"}, {"--..", "Z"},
  {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"}, {"....-", "4"}, {".....", "5"},
  {"-....", "6"}, {"--...", "7"}, {"---..", "8"}, {"----.", "9"},
  {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {"-..-.", "/"}, {"-...-", "="}, {".-.-.", "+"},
  {"-....-", "-"}, {".--.-.", "@"}, {"---...", ":"}, {".----.", "'"}, {"-.--.", "("}, {"-.--.-", ")"},
  {".-..-.", "\""}, {"-.-.--", "!"}, {"..--.-", "_"}, {"-.-.-.", ";"},
  {"...-.-", "<SK>"}, {".-...", "<AS>"}, {"........", "<HH>"}, {"...-.", "<SN>"}, {NULL, NULL}};

static const char *lookup(const char *code)
{
  int i;
  for (i = 0; MORSE[i].code; i++)
  {
    if (!strcmp(MORSE[i].code, code)) return MORSE[i].txt;
  }
  return "*";
}

// Goertzel sur un bloc : puissance de la raie
static float goertzel(const float *x, int n, float coef)
{
  float s0, s1 = 0, s2 = 0;
  int i;
  for (i = 0; i < n; i++)
  {
    s0 = x[i] + coef * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return s1 * s1 + s2 * s2 - coef * s1 * s2;
}

static void status(const char *s)
{
  static char last[128];
  if (strcmp(s, last))
  {
    snprintf(last, sizeof last, "%s", s);
    printf("### %s\n", s);
  }
}

// Point (ms) et biais de mesure a partir des dernieres marques et des derniers
// silences. Deux groupes de marques (k-moyennes) : points et traits ; un seul
// groupe : les silences entre elements (un point) disent si ce sont des points
// ou des traits. Le biais (marques allongees, silences raccourcis d'autant par
// le lissage de l'enveloppe) vaut la moitie de l'ecart point / silence court.
static float estimate_unit(const float *m, int nm, const float *s, int ns, float cur, float *bias)
{
  float lo = 1e9f, hi = 0, clo, chi, ss = 0, sshort, unit;
  int i, it, nss = 0;
  if (nm < 2) return cur;
  for (i = 0; i < nm; i++)
  {
    if (m[i] < lo) lo = m[i];
    if (m[i] > hi) hi = m[i];
  }
  clo = lo;
  chi = hi;
  for (it = 0; it < 4; it++)
  {
    float slo = 0, shi = 0;
    int nlo = 0, nhi = 0;
    for (i = 0; i < nm; i++)
    {
      if (fabsf(m[i] - clo) <= fabsf(m[i] - chi)) { slo += m[i]; nlo++; }
      else { shi += m[i]; nhi++; }
    }
    if (nlo) clo = slo / nlo;
    if (nhi) chi = shi / nhi;
  }
  for (i = 0; i < ns; i++)
  {
    if (s[i] < 2 * clo) { ss += s[i]; nss++; }
  }
  sshort = nss ? ss / nss : clo;
  if (chi > 1.8f * clo)                   // points et traits
  {
    *bias = (clo - sshort) / 2;
    unit = ((clo - *bias) + (chi - *bias) / 3) / 2;
  }
  else                                    // un seul groupe
  {
    float c = (clo + chi) / 2;
    if (nss && (c > 2 * sshort)) { *bias = 0; unit = c / 3; }             // traits
    else { *bias = nss ? (c - sshort) / 2 : 0; unit = c - *bias; }       // points
  }
  if (*bias < 0) *bias = 0;
  if (*bias > unit / 2) *bias = unit / 2;
  return (unit < UNIT_MIN) ? UNIT_MIN : ((unit > UNIT_MAX) ? UNIT_MAX : unit);
}

// Code morse d'une lettre : chaque marque, corrigee du biais, est un trait
// au-dela de deux points
static const char *letter(const float *m, int n, float unit, float bias)
{
  static char code[16];
  int i;
  for (i = 0; i < n && i < 15; i++) code[i] = (m[i] - bias > 2 * unit) ? '-' : '.';
  code[i] = 0;
  return code;
}

int main(int argc, char **argv)
{
  int rate = 22050, i, blk, hop, k;
  float coef[NBIN], pw[NBIN] = {0};
  float *x, *win;
  int16_t *in;
  size_t have = 0;
  // suivi de la tonalite
  int tone = -1, cand = -1, cand_n = 0;
  // enveloppe, seuil
  float env = 0, floor_ = 1, peak = 1;
  bool key = false, pend = false, locked = false;
  int pend_n = 0;
  // chronometrage (ms)
  float unit = 60, bias = 0, mark_ms = 0, space_ms = 0;
  float marks[HIST], spaces[HIST];
  int nm = 0, ns = 0, im = 0, is = 0;
  float cur[12];                          // marques de la lettre en cours (ms)
  int ncode = 0, since_status = 0, idle_blocks = 0;
  bool word_gap = true;                   // espace deja emis
  bool debug = getenv("MD_CW_DEBUG") != NULL;
  for (i = 1; i < argc - 1; i++)
  {
    if (!strcmp(argv[i], "-r")) rate = atoi(argv[++i]);
  }
  if (rate < 8000) rate = 22050;
  blk = (int)(rate * BLK_MS / 1000 + 0.5f);
  hop = blk / 2;
  x = malloc(blk * sizeof(float));
  win = calloc(blk, sizeof(float));
  in = malloc(hop * 2);
  if (!x || !win || !in) return 1;
  for (k = 0; k < NBIN; k++) coef[k] = 2 * cosf(2 * (float)M_PI * (F_LO + k * F_STEP) / rate);
  setvbuf(stdout, NULL, _IONBF, 0);
  status("Waiting for a signal");
  for (;;)
  {
    ssize_t r = read(0, (char *)in + have, hop * 2 - have);
    float best = 0, noise = 0, on_thr, off_thr;
    int ibest = 0, nn = 0;
    bool nkey;
    if (r <= 0) break;
    have += r;
    if (have < (size_t)hop * 2) continue;
    have = 0;
    memmove(win, win + hop, (blk - hop) * sizeof(float));
    for (i = 0; i < hop; i++) win[blk - hop + i] = in[i];
    for (i = 0; i < blk; i++)
    {
      float w = 0.5f - 0.5f * cosf(2 * (float)M_PI * i / blk);   // Hann : moins de fuite entre raies
      x[i] = win[i] * w;
    }
    for (k = 0; k < NBIN; k++)
    {
      float p = goertzel(x, blk, coef[k]);
      pw[k] += 0.4f * (p - pw[k]);        // lissage ~2 pas
      if (pw[k] > best) { best = pw[k]; ibest = k; }
    }
    for (k = 0; k < NBIN; k++)            // bruit : raies loin de la plus forte
    {
      if (abs(k - ibest) > 2) { noise += pw[k]; nn++; }
    }
    noise = nn ? noise / nn : 1;
    if (noise < 1) noise = 1;
    // suivi de la tonalite : la raie la plus forte, si elle emerge du bruit ;
    // derive : recentrage immediat sur une raie voisine plus forte ; autre
    // station : changement apres 0,25 s a +6 dB
    if (tone < 0)
    {
      if (best > 10 * noise) { tone = ibest; peak = best; floor_ = noise; }
    }
    else if ((abs(ibest - tone) <= 2) && (ibest != tone) && (pw[ibest] > 1.5f * pw[tone]) && (best > 10 * noise))
    {
      tone = ibest;
      cand_n = 0;
    }
    else if ((abs(ibest - tone) > 1) && (pw[ibest] > 4 * (pw[tone] > peak ? pw[tone] : peak)) && (best > 10 * noise))
    {
      if (ibest == cand) cand_n++;
      else { cand = ibest; cand_n = 1; }
      if (cand_n >= 40) { tone = cand; peak = best; cand_n = 0; }
    }
    else cand_n = 0;
    if (tone < 0)
    {
      continue;
    }
    // enveloppe : la raie suivie et ses voisines (derive)
    env = pw[tone];
    if ((tone > 0) && (pw[tone - 1] > env)) env = pw[tone - 1];
    if ((tone < NBIN - 1) && (pw[tone + 1] > env)) env = pw[tone + 1];
    // crete (attaque rapide, decroissance en ~15 points : suit l'evanouissement
    // sans retomber pendant un espace de mot) et plancher (descente rapide,
    // remontee lente)
    {
      float tau = 15 * unit;
      if (tau < 800) tau = 800;
      if (tau > 4000) tau = 4000;
      peak = (env > peak) ? env : peak * expf(-HOP_MS / tau);
      floor_ = (env < floor_) ? env : floor_ * (1 + HOP_MS / 2900);
    }
    if (floor_ < noise) floor_ = noise;
    if (peak < 4 * floor_)                // moins de 6 dB : rien a manipuler
    {
      nkey = false;
      locked = false;
    }
    else
    {
      locked = true;
      on_thr = floor_ + 0.5f * (peak - floor_);
      off_thr = floor_ + 0.3f * (peak - floor_);
      nkey = key ? (env > off_thr) : (env > on_thr);
    }
    // anti-rebond : un changement doit tenir deux pas
    if (nkey != key)
    {
      if ((pend_n > 0) && (nkey == pend)) pend_n++;
      else { pend = nkey; pend_n = 1; }
      if (pend_n < 2) nkey = key;
    }
    else pend_n = 0;
    // chronometrage
    if (nkey == key)
    {
      if (key) mark_ms += HOP_MS;
      else space_ms += HOP_MS;
    }
    else if (nkey)                        // debut d'une marque : le silence precedent
    {
      float s = space_ms + bias;
      if (debug) fprintf(stderr, "s%.0f ", space_ms);
      if (nm && (space_ms > 0))
      {
        spaces[is] = space_ms;
        is = (is + 1) % HIST;
        if (ns < HIST) ns++;
      }
      if (ncode && (s > 2 * unit))
      {
        fputs(lookup(letter(cur, ncode, unit, bias)), stdout);
        ncode = 0;
        word_gap = false;
      }
      if (!word_gap && (s > 5 * unit))
      {
        fputs(" ", stdout);
        word_gap = true;
      }
      key = true;
      mark_ms = HOP_MS;
    }
    else                                  // fin d'une marque
    {
      if (debug) fprintf(stderr, "M%.0f ", mark_ms);
      marks[im] = mark_ms;
      im = (im + 1) % HIST;
      if (nm < HIST) nm++;
      unit = estimate_unit(marks, nm, spaces, ns, unit, &bias);
      if (ncode < 12) cur[ncode++] = mark_ms;
      key = false;
      space_ms = HOP_MS;
    }
    // silence prolonge : fin de lettre, puis espace de mot
    if (!key && ncode && (space_ms + bias > 2 * unit))
    {
      fputs(lookup(letter(cur, ncode, unit, bias)), stdout);
      ncode = 0;
      word_gap = false;
    }
    if (!key && !word_gap && (space_ms + bias > 5 * unit))
    {
      fputs(" ", stdout);
      word_gap = true;
    }
    // etat toutes les 2 s ; sans signal depuis 10 s, la tonalite est relachee
    idle_blocks = locked ? 0 : idle_blocks + 1;
    if (idle_blocks > 10000 / HOP_MS)
    {
      tone = -1;
      nm = ns = 0;
      status("Waiting for a signal");
      idle_blocks = 0;
      continue;
    }
    if (++since_status >= 2000 / HOP_MS)
    {
      char s[128];
      since_status = 0;
      if (locked)
      {
        snprintf(s, sizeof s, "Tone %d Hz   %d WPM   S/N %.0f dB", F_LO + tone * F_STEP, (int)(1200 / unit + 0.5f),
                 10 * log10f(peak / floor_));
      }
      else
      {
        snprintf(s, sizeof s, "Tone %d Hz   no keying", F_LO + tone * F_STEP);
      }
      status(s);
    }
  }
  if (ncode) fputs(lookup(letter(cur, ncode, unit, bias)), stdout);
  fputs("\n", stdout);
  return 0;
}
