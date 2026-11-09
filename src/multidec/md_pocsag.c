// md_pocsag.c : decodeur POCSAG (512, 1200 et 2400 bauds) du decodeur
// multiple, a partir de l'audio FM demodule (16 bits signes, mono, stdin).
// Portage en C du decodeur de HTMOD (Pocsag.java, lui-meme issu de Radtel
// Tools) : sur un signal faible il decode la ou multimon-ng ne sort que des
// adresses (essai du 9/11/2026, reseau SDIS 173,7 MHz, rafale a 1 dB au-dessus
// du souffle : 7 messages corrects contre 0).
//
// Chaine : anti-continu -> moyenne glissante (~1/4 de bit) -> bascule a
// hysteresis (35 % de la crete suivie) -> DPLL calee sur les transitions ->
// integration sur le bit -> registre 32 bits -> synchro 0x7CD215D8 (ou son
// inverse : polarite inconnue) -> lots de 16 mots -> correction BCH(31,21)
// jusqu'a 2 bits -> messages. Les trois debits sont decodes en parallele.
//
// Sortie : une ligne JSON par message, comme multimon-ng --json, lue par
// multidec (parse_multimon) :
//   {"demod_name":"POCSAG512","address":28328,"function":0,"alpha":"..."}
// Le choix texte / numerique se fait au contenu (les reseaux SDIS envoient du
// texte avec la fonction 0), sauf si -f alpha ou -f numeric l'impose.
//
// Usage : md_pocsag -r <frequence> [-b 512,1200,2400] [-f auto|alpha|numeric]
//                   [-C US|FR|DE|SE|SI] [-e] [-p]
//   -e : ne pas sortir les appels sans message (bip seul)
//   -p : sortir aussi les messages douteux (mots non corrigeables et texte
//        illisible : residus de fin de rafale), caches par defaut

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SYNC 0x7CD215D8u
#define IDLE 0x7A89C197u
#define GEN  0x769u                       // x^10+x^9+x^8+x^6+x^5+x^3+1
#define PAYLOAD_MAX 512                   // mots de 20 bits d'un message
#define NUMERIC_CHARS "0123456789*U -)("

static int bauds[3] = {512, 1200, 2400};
static int nbauds = 3;
static int force;                         // 0 auto, 1 alpha, 2 numerique
static bool hide_tone, show_partial;
static const char *trtab[128];            // caracteres nationaux (option Charset)

// ---------------------------------------------------------------- BCH(31,21)

static int syndromes[1024];               // syndrome -> masque d'erreur (1 ou 2 bits), -1 inconnu

static unsigned bch_rem(unsigned v)
{
  unsigned r = v;
  int i;
  for (i = 30; i >= 10; i--)
  {
    if ((r >> i) & 1) r ^= GEN << (i - 10);
  }
  return r;
}

static void bch_init(void)
{
  int i, j;
  for (i = 0; i < 1024; i++) syndromes[i] = -1;
  for (i = 0; i < 31; i++)
  {
    syndromes[bch_rem(1u << i)] = 1 << i;
    for (j = i + 1; j < 31; j++)
    {
      unsigned m = (1u << i) | (1u << j), s = bch_rem(m);
      if (syndromes[s] == -1) syndromes[s] = (int)m;
    }
  }
}

// Mot corrige dans *out, nombre de bits corriges, ou -1 si plus de 2 erreurs
static int bch_correct(unsigned cw, unsigned *out)
{
  unsigned c31 = cw >> 1, s = bch_rem(c31), fixed, parity;
  int mask = (s == 0) ? 0 : syndromes[s], errors;
  if (mask == -1) return -1;
  fixed = c31 ^ (unsigned)mask;
  parity = __builtin_popcount(fixed) & 1;
  errors = __builtin_popcount((unsigned)mask) + ((parity != (cw & 1)) ? 1 : 0);
  if (errors > 2) return -1;
  *out = (fixed << 1) | parity;
  return errors;
}

// ---------------------------------------------------------------- messages

static unsigned reverse_bits(unsigned v, int n)
{
  unsigned r = 0;
  int i;
  for (i = 0; i < n; i++)
  {
    if ((v >> i) & 1) r |= 1u << (n - 1 - i);
  }
  return r;
}

static void rtrim(char *s)
{
  int n = strlen(s);
  while ((n > 0) && ((s[n - 1] == ' ') || (s[n - 1] == '\n') || (s[n - 1] == '\t') || (s[n - 1] == '\r'))) s[--n] = 0;
}

static void decode_numeric(const unsigned *payload, int n, char *out, int size)
{
  int i, k, p = 0;
  for (i = 0; i < n; i++)
  {
    for (k = 4; k >= 0; k--)
    {
      if (p < size - 1) out[p++] = NUMERIC_CHARS[reverse_bits((payload[i] >> (k * 4)) & 0xF, 4)];
    }
  }
  out[p] = 0;
  rtrim(out);
  // espaces de tete comme Java trim()
  p = 0;
  while (out[p] == ' ') p++;
  if (p) memmove(out, out + p, strlen(out + p) + 1);
}

// Texte 7 bits (poids faible en tete) ; caracteres de commande inattendus
// remplaces par 0x7f (comptes comme douteux, rendus "?")
static void decode_alpha(const unsigned *payload, int n, char *out, int size)
{
  unsigned acc = 0;
  int nbits = 0, i, b, p = 0;
  for (i = 0; i < n; i++)
  {
    for (b = 19; b >= 0; b--)
    {
      acc = (acc << 1) | ((payload[i] >> b) & 1);
      if (++nbits == 7)
      {
        unsigned c = reverse_bits(acc, 7);
        acc = 0;
        nbits = 0;
        if (p >= size - 1) continue;
        if (c == 10) out[p++] = '\n';
        else if ((c >= 32) && (c <= 126)) out[p++] = (char)c;
        else if ((c != 0) && (c != 3) && (c != 4) && (c != 0x17) && (c != 13)) out[p++] = 0x7f;
      }
    }
  }
  out[p] = 0;
  rtrim(out);
}

// Choix numerique / texte au contenu (comme HTMOD et le mode auto de multimon-ng)
static bool guess_numeric(int function, const char *num, const char *alpha)
{
  int odd = 0, bad = 0, ln = strlen(num), la = strlen(alpha);
  const char *p;
  double num_odd, alpha_odd;
  if (!la) return true;
  for (p = num; *p; p++) if (!strchr("0123456789 -", *p)) odd++;
  for (p = alpha; *p; p++) if (*p == 0x7f) bad++;
  num_odd = odd / (double)(ln > 0 ? ln : 1);
  alpha_odd = bad / (double)la;
  if (num_odd < alpha_odd) return true;
  if (num_odd > alpha_odd) return false;
  return (function == 0) && (ln <= 16);
}

static void json_text(const char *s)
{
  for (; *s; s++)
  {
    unsigned char c = *s;
    if (c == 0x7f) fputs("?", stdout);
    else if ((c == '"') || (c == '\\')) printf("\\%c", c);
    else if (c == '\n') fputs("\\n", stdout);
    else if (c < 32) printf("\\u%04x", c);
    else if ((c < 127) && trtab[c]) fputs(trtab[c], stdout);
    else putchar(c);
  }
}

static void emit(int baud, unsigned address, int function, const unsigned *payload, int n, int corrected, int bad)
{
  char num[PAYLOAD_MAX * 5 + 1], alpha[PAYLOAD_MAX * 3 + 1];
  bool numeric;
  if ((n == 0) && hide_tone) return;
  if (n > 0)
  {
    decode_numeric(payload, n, num, sizeof num);
    decode_alpha(payload, n, alpha, sizeof alpha);
    numeric = (force == 2) || ((force == 0) && guess_numeric(function, num, alpha));
    if (!show_partial && (bad > 0) && !numeric)    // residu de fin de rafale : texte illisible
    {
      int q = 0;
      const char *p;
      for (p = alpha; *p; p++) if (*p == 0x7f) q++;
      if (q * 4 >= (int)strlen(alpha)) return;
    }
  }
  printf("{\"demod_name\":\"POCSAG%d\",\"address\":%u,\"function\":%d", baud, address, function);
  if (n > 0)
  {
    printf(",\"%s\":\"", numeric ? "numeric" : "alpha");
    json_text(numeric ? num : alpha);
    putchar('"');
  }
  printf(",\"corrected\":%d,\"bad\":%d}\n", corrected, bad);
  fflush(stdout);
}

// ---------------------------------------------------------------- demodulateur (un par debit)

typedef struct {
  int baud;
  double step;                            // bits par echantillon
  int ma_len;                             // moyenne glissante
  float *ring;
  int ring_idx;
  float ma_sum;
  float peak_decay, peak;
  bool state;
  double phase;
  int integ;
  unsigned shift;
  bool synced, inverted;
  int bits_in_cw, cw_index;               // cw_index 16 : synchro attendue
  int bad_in_batch;
  // message en cours
  bool building;
  unsigned address;
  int function;
  unsigned payload[PAYLOAD_MAX];
  int npayload, corrected, bad;
} demod;

static void demod_init(demod *d, int rate, int baud)
{
  double spb = rate / (double)baud;
  memset(d, 0, sizeof *d);
  d->baud = baud;
  d->step = 1.0 / spb;
  d->ma_len = (int)(spb / 4 + 0.5);
  if (d->ma_len < 1) d->ma_len = 1;
  d->ring = calloc(d->ma_len, sizeof(float));
  d->peak_decay = (float)exp(-1.0 / (spb * 32));
}

static void finish_message(demod *d)
{
  if (!d->building) return;
  d->building = false;
  emit(d->baud, d->address, d->function, d->payload, d->npayload, d->corrected, d->bad);
}

static void lose_sync(demod *d)
{
  finish_message(d);
  d->synced = false;
  d->cw_index = 0;
  d->bits_in_cw = 0;
}

static void start_sync(demod *d, bool inv)
{
  d->synced = true;
  d->inverted = inv;
  d->bits_in_cw = 0;
  d->cw_index = 0;
  d->bad_in_batch = 0;
}

static void add_payload(demod *d, unsigned w)
{
  if (d->npayload < PAYLOAD_MAX) d->payload[d->npayload++] = w;
}

static void codeword(demod *d, unsigned raw, int index)
{
  unsigned cw;
  int n = bch_correct(raw, &cw);
  if (n < 0)
  {
    if (++d->bad_in_batch >= 6)           // trop d'erreurs : bruit
    {
      lose_sync(d);
      return;
    }
    if (d->building)
    {
      d->bad++;
      add_payload(d, (raw >> 11) & 0xFFFFF);
    }
    return;
  }
  if (cw == IDLE)
  {
    finish_message(d);
    return;
  }
  if (!(cw & 0x80000000u))                // mot d'adresse
  {
    finish_message(d);
    d->building = true;
    d->address = (((cw >> 13) & 0x3FFFF) << 3) | (unsigned)(index / 2);
    d->function = (cw >> 11) & 3;
    d->npayload = 0;
    d->corrected = n;
    d->bad = 0;
  }
  else if (d->building)
  {
    d->corrected += n;
    add_payload(d, (cw >> 11) & 0xFFFFF);
  }
}

static void bit(demod *d, bool b)
{
  unsigned cw;
  d->shift = (d->shift << 1) | (b ? 1u : 0u);
  if (!d->synced)
  {
    if (__builtin_popcount(d->shift ^ SYNC) <= 1) start_sync(d, false);
    else if (__builtin_popcount(d->shift ^ ~SYNC) <= 1) start_sync(d, true);
    return;
  }
  if (++d->bits_in_cw < 32) return;
  d->bits_in_cw = 0;
  cw = d->inverted ? ~d->shift : d->shift;
  if (d->cw_index == 16)                  // entre deux lots : synchro, sinon fin de transmission
  {
    if (__builtin_popcount(cw ^ SYNC) <= 2)
    {
      d->cw_index = 0;
      d->bad_in_batch = 0;
    }
    else lose_sync(d);
    return;
  }
  codeword(d, cw, d->cw_index);
  d->cw_index++;
}

static void sample(demod *d, float x)
{
  float v, a, thr;
  bool ns;
  d->ma_sum += x - d->ring[d->ring_idx];
  d->ring[d->ring_idx] = x;
  d->ring_idx = (d->ring_idx + 1) % d->ma_len;
  v = d->ma_sum / d->ma_len;
  a = fabsf(v);
  d->peak = (a > d->peak) ? a : d->peak * d->peak_decay;
  thr = d->peak * 0.35f;
  ns = d->state ? (v > -thr) : (v > thr);
  if (ns != d->state)
  {
    double k = d->synced ? 0.06 : 0.2;    // transition attendue en limite de bit
    d->state = ns;
    d->phase += (d->phase < 0.5) ? -d->phase * k : (1.0 - d->phase) * k;
  }
  d->integ += d->state ? 1 : -1;
  d->phase += d->step;
  if (d->phase >= 1.0)
  {
    d->phase -= 1.0;
    bit(d, d->integ > 0);
    d->integ = 0;
  }
}

// ---------------------------------------------------------------- caracteres nationaux

static void init_charset(const char *cs)
{
  if (!strcmp(cs, "FR"))
  {
    trtab[0x24] = "\xc2\xa3"; trtab[0x40] = "\xc3\xa0"; trtab[0x5b] = "\xc2\xb0"; trtab[0x5c] = "\xc3\xa7";
    trtab[0x5d] = "\xc2\xa7"; trtab[0x60] = "\xc2\xb5"; trtab[0x7b] = "\xc3\xa9"; trtab[0x7c] = "\xc3\xb9";
    trtab[0x7d] = "\xc3\xa8"; trtab[0x7e] = "\xc2\xa8";
  }
  else if (!strcmp(cs, "DE"))
  {
    trtab[0x5b] = "\xc3\x84"; trtab[0x5c] = "\xc3\x96"; trtab[0x5d] = "\xc3\x9c";
    trtab[0x7b] = "\xc3\xa4"; trtab[0x7c] = "\xc3\xb6"; trtab[0x7d] = "\xc3\xbc"; trtab[0x7e] = "\xc3\x9f";
  }
  else if (!strcmp(cs, "SE"))
  {
    trtab[0x5b] = "\xc3\x84"; trtab[0x5c] = "\xc3\x96"; trtab[0x5d] = "\xc3\x85";
    trtab[0x7b] = "\xc3\xa4"; trtab[0x7c] = "\xc3\xb6"; trtab[0x7d] = "\xc3\xa5";
  }
  else if (!strcmp(cs, "SI"))
  {
    trtab[0x40] = "\xc5\xbd"; trtab[0x5b] = "\xc5\xa0"; trtab[0x5c] = "\xc4\x90"; trtab[0x5d] = "\xc4\x86";
    trtab[0x5e] = "\xc4\x8c"; trtab[0x60] = "\xc5\xbe"; trtab[0x7b] = "\xc5\xa1"; trtab[0x7c] = "\xc4\x91";
    trtab[0x7d] = "\xc4\x87"; trtab[0x7e] = "\xc4\x8d";
  }
}

int main(int argc, char **argv)
{
  int rate = 22050, i, n;
  demod d[3];
  float dc_x = 0, dc_y = 0;
  int16_t buf[2048];
  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-r") && (i + 1 < argc)) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-b") && (i + 1 < argc))
    {
      char s[64], *t;                     // copie : strtok ne doit pas retoucher la ligne de commande (ps)
      snprintf(s, sizeof s, "%s", argv[++i]);
      nbauds = 0;
      for (t = strtok(s, ","); t && (nbauds < 3); t = strtok(NULL, ","))
      {
        int b = atoi(t);
        if ((b == 512) || (b == 1200) || (b == 2400)) bauds[nbauds++] = b;
      }
      if (!nbauds) { bauds[0] = 512; bauds[1] = 1200; bauds[2] = 2400; nbauds = 3; }
    }
    else if (!strcmp(argv[i], "-f") && (i + 1 < argc))
    {
      const char *m = argv[++i];
      force = !strcmp(m, "alpha") ? 1 : (!strcmp(m, "numeric") ? 2 : 0);
    }
    else if (!strcmp(argv[i], "-C") && (i + 1 < argc)) init_charset(argv[++i]);
    else if (!strcmp(argv[i], "-e")) hide_tone = true;
    else if (!strcmp(argv[i], "-p")) show_partial = true;
  }
  if (rate < 8000) rate = 22050;
  bch_init();
  for (i = 0; i < nbauds; i++) demod_init(&d[i], rate, bauds[i]);
  for (;;)
  {
    ssize_t r = read(0, buf, sizeof buf);
    if (r <= 0) break;
    n = r / 2;
    for (i = 0; i < n; i++)
    {
      float x = buf[i], y = x - dc_x + 0.999f * dc_y;   // passe-haut ~7 Hz
      int k;
      dc_x = x;
      dc_y = y;
      for (k = 0; k < nbauds; k++) sample(&d[k], y);
    }
  }
  for (i = 0; i < nbauds; i++) lose_sync(&d[i]);     // fin de reception : messages en cours
  return 0;
}
