// md_fmdemod.c : ecoute d'une radiosonde. Demodule en FM le signal IQ de
// rtl_fm -M raw (16 bits signes entrelaces, stdin) et ecrit le son (16 bits
// mono, meme frequence, stdout) pour md_audio.
// Usage : md_fmdemod -r <frequence> [-s 0] [-b <kHz>]
//   -s 0 : sans squelch, le souffle passe
//   -b   : largeur du filtre du son (IQ, passe-bas FIR de Blackman, 49
//          coefficients) ; $MD_SHM/sonde_bw (kHz, ecrit par multidec au type de
//          sonde decode) la remplace en cours de route. 0 : sans filtre.
//
// Le squelch mesure le signal NON filtre : filtre, le bruit a une variance de
// phase proche de celle de la modulation (essai : 0,3 rad2 a 10 kHz) et ne se
// distingue plus d'une sonde. Le filtre ne sert qu'a retirer le souffle hors
// du canal dans le son.
//
// Squelch au bruit : sans porteuse, l'ecart de phase entre deux echantillons
// est quelconque (variance ~3,3 rad2) ; avec une sonde (deviation de 2,4 a
// 4,8 kHz a 48 kHz) elle reste sous ~0,5 rad2. Le son est ouvert sous 1 rad2 ;
// un bloc douteux (1 a 2 rad2, signal faible) le laisse ouvert 0,5 s, un bloc
// de bruit franc (au-dela de 2 rad2) le ferme aussitot (pas de souffle a la fin).

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLK_MS 20                         // bloc de mesure du squelch
#define OPEN_VAR 1.0f                     // variance de phase (rad2) sous laquelle le son passe
#define NOISE_VAR 2.0f                    // au-dela : bruit seul, le son est coupe
#define HANG_BLK 25                       // maintien : 25 x 20 ms
#define TAPS 49                           // 4 x 48 kHz / 4 kHz de transition

static float taps[TAPS];
static int ntaps;                         // 0 : son non filtre

// Passe-bas de largeur totale bw (Hz) a la frequence fs, fenetre de Blackman,
// gain 1 au centre
static void design(float bw, int fs)
{
  int n;
  float fc = bw / 2 / fs, g = 0;
  ntaps = (bw > 0) && (bw < fs) ? TAPS : 0;
  for (n = 0; n < ntaps; n++)
  {
    float m = n - (TAPS - 1) / 2.0f, x = 2 * (float)M_PI * fc * m;
    float w = 0.42f - 0.5f * cosf(2 * (float)M_PI * n / (TAPS - 1)) + 0.08f * cosf(4 * (float)M_PI * n / (TAPS - 1));
    taps[n] = (m == 0 ? 2 * fc : 2 * fc * sinf(x) / x) * w;
    g += taps[n];
  }
  for (n = 0; n < ntaps; n++) taps[n] /= g;
}

// Largeur demandee par multidec (kHz), lue chaque seconde ; < 0 : pas de fichier
static float read_bw(const char *path)
{
  FILE *f = fopen(path, "r");
  float v = -1;
  if (f)
  {
    if (fscanf(f, "%f", &v) != 1) v = -1;
    fclose(f);
  }
  return v;
}

static int write_all(const void *p, size_t n)
{
  const char *c = p;
  while (n)
  {
    ssize_t w = write(1, c, n);
    if (w <= 0) return -1;
    c += w;
    n -= w;
  }
  return 0;
}

int main(int argc, char **argv)
{
  int rate = 48000, i, blk, hang = 0, squelch = 1, nb = 0, pos = 0;
  int16_t *in, *out;
  size_t have = 0;
  float pi = 1, pq = 0, dc = 0, fi = 1, fq = 0, bw = 0, want;
  float hi[TAPS] = {0}, hq[TAPS] = {0};   // dernieres valeurs (filtre)
  char bwpath[160];
  const float scale = 32767.0f * 3 / (float)M_PI;   // 0,31 rad (RS41) -> ~9800
  for (i = 1; i < argc - 1; i++)
  {
    if (!strcmp(argv[i], "-r")) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-s")) squelch = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-b")) bw = atof(argv[++i]) * 1000;
  }
  if (rate < 8000) rate = 48000;
  snprintf(bwpath, sizeof bwpath, "%s/sonde_bw", getenv("MD_SHM") ? getenv("MD_SHM") : "/dev/shm/multidec");
  design(bw, rate);
  blk = rate * BLK_MS / 1000;
  in = malloc(blk * 4);
  out = malloc(blk * 2);
  if (!in || !out)
  {
    return 1;
  }
  for (;;)
  {
    ssize_t r = read(0, (char *)in + have, blk * 4 - have);
    double s = 0, s2 = 0;
    float var;
    int k;
    if (r <= 0)
    {
      break;
    }
    have += r;
    if (have < (size_t)blk * 4)
    {
      continue;
    }
    have = 0;
    if (++nb >= 1000 / BLK_MS)            // chaque seconde : largeur voulue par multidec
    {
      nb = 0;
      want = read_bw(bwpath);
      if ((want >= 0) && (fabsf(want * 1000 - bw) > 1))
      {
        bw = want * 1000;
        design(bw, rate);
      }
    }
    for (k = 0; k < blk; k++)
    {
      float x = in[2 * k], y = in[2 * k + 1];
      float d = atan2f(y * pi - x * pq, x * pi + y * pq);   // phase de l'echantillon fois le precedent conjugue
      float v, ds, ux = x, uy = y;
      int j;
      pi = x;
      pq = y;
      s += d;                            // squelch : signal non filtre
      s2 += d * d;
      if (ntaps)                         // son : signal filtre dans le canal
      {
        hi[pos] = x;
        hq[pos] = y;
        ux = uy = 0;
        for (j = 0; j < ntaps; j++)
        {
          int q = pos - j < 0 ? pos - j + TAPS : pos - j;
          ux += taps[j] * hi[q];
          uy += taps[j] * hq[q];
        }
        pos = (pos + 1) % TAPS;
      }
      ds = atan2f(uy * fi - ux * fq, ux * fi + uy * fq);
      fi = ux;
      fq = uy;
      dc += 0.0005f * (ds - dc);         // decalage de frequence (correction de la cle) : retire
      v = (ds - dc) * scale;
      out[k] = (int16_t)(v > 32767 ? 32767 : (v < -32767 ? -32767 : v));
    }
    var = (float)(s2 / blk - (s / blk) * (s / blk));
    if (var < OPEN_VAR) hang = HANG_BLK;
    else if (var > NOISE_VAR) hang = 0;
    else if (hang > 0) hang--;
    if (squelch && !hang)
    {
      memset(out, 0, blk * 2);
    }
    if (write_all(out, blk * 2))
    {
      break;
    }
  }
  return 0;
}
