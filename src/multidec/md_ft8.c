// md_ft8.c : FT8/FT4 pour le decodeur multiple. Lit l'audio BLU (16 bits,
// mono) sur stdin, le decoupe en creneaux alignes sur l'heure UTC (15 s en
// FT8, 7,5 s en FT4 : l'heure du Pi doit etre juste, NTP), et lance
// decode_ft8 (ft8_lib) sur chaque creneau complet. Chaque decodage est ecrit
// sur stdout :  "FT8 HHMMSS <snr> <dt> <freq> ~  <message>"
// Ecrit aussi le niveau audio dans /dev/shm/multidec/level (comme md_level).
// Usage : md_ft8 -r <frequence> [-4]

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIR "/dev/shm/multidec"

static void put32(unsigned char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static int write_wav(const char *path, const int16_t *s, long n, int rate)
{
  unsigned char h[44];
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  memcpy(h, "RIFF", 4); put32(h + 4, 36 + n * 2); memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, rate);
  put32(h + 28, rate * 2); put16(h + 32, 2); put16(h + 34, 16);
  memcpy(h + 36, "data", 4); put32(h + 40, n * 2);
  fwrite(h, 1, 44, f);
  fwrite(s, 2, n, f);
  fclose(f);
  return 0;
}

// Processus fils : decode le fichier, ecrit les resultats, efface le fichier
static void decode(const char *wav, const char *mode, time_t slot)
{
  char cmd[160], ln[200], ts[8];
  struct tm tm;
  FILE *p;
  gmtime_r(&slot, &tm);
  strftime(ts, sizeof ts, "%H%M%S", &tm);
  snprintf(cmd, sizeof cmd, "decode_ft8 %s %s 2>/dev/null", strcmp(mode, "FT4") ? "" : "-ft4", wav);
  p = popen(cmd, "r");
  if (p)
  {
    while (fgets(ln, sizeof ln, p))
    {
      char *sp = strchr(ln, ' ');
      if (sp && strchr(ln, '~'))
      {
        printf("%s %s%s", mode, ts, sp);
        fflush(stdout);
      }
    }
    pclose(p);
  }
  unlink(wav);
  _exit(0);
}

int main(int argc, char **argv)
{
  int rate = 12000, i, which = 0;
  double slotlen = 15.0;
  const char *mode = "FT8";
  int16_t *buf, chunk[2048];
  long cap, n = 0, cur = -1, lv_n = 0;
  double lv = 0;
  pid_t child = 0;

  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-r") && (i + 1 < argc)) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-4")) { slotlen = 7.5; mode = "FT4"; }
  }
  signal(SIGPIPE, SIG_DFL);
  mkdir(DIR, 0755);
  cap = (long)(rate * slotlen) + rate;
  buf = malloc(cap * sizeof(int16_t));
  if (!buf) return 1;

  for (;;)
  {
    struct timespec ts;
    ssize_t r = read(0, chunk, sizeof chunk);
    long slot;
    if (r <= 0) break;
    clock_gettime(CLOCK_REALTIME, &ts);
    slot = (long)floor((ts.tv_sec + ts.tv_nsec * 1e-9) / slotlen);
    if (slot != cur)
    {
      // creneau termine : decode s'il est presque complet (>= 80 %)
      if ((cur >= 0) && (n >= rate * slotlen * 0.8))
      {
        char wav[64];
        if (child > 0 && waitpid(child, NULL, WNOHANG) == 0)
        {
          // decodage precedent pas fini : creneau abandonne
        }
        else
        {
          snprintf(wav, sizeof wav, DIR "/ft8_%d.wav", which);
          which ^= 1;
          write_wav(wav, buf, n, rate);
          child = fork();
          if (child == 0) decode(wav, mode, (time_t)(cur * slotlen));
        }
      }
      cur = slot;
      n = 0;
    }
    for (i = 0; i < r / 2; i++)
    {
      if (n < cap) buf[n++] = chunk[i];
      lv += (chunk[i] / 32768.0) * (chunk[i] / 32768.0);
      if (++lv_n >= rate / 4)
      {
        char s[48];
        int fd, len = snprintf(s, sizeof s, "%.1f -99\n", 10 * log10(lv / lv_n + 1e-12));
        fd = open(DIR "/level.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
          if (write(fd, s, len) == len) rename(DIR "/level.tmp", DIR "/level");
          close(fd);
        }
        lv = 0;
        lv_n = 0;
      }
    }
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
  }
  return 0;
}
