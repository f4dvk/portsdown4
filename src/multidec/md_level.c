// md_level.c : recopie stdin -> stdout (echantillons 16 bits signes) et
// ecrit le niveau toutes les 250 ms dans /dev/shm/multidec/level, pour la
// barre de niveau du decodeur multiple :
//   "<niveau dBFS> <bruit dBFS>"
// Le "bruit" (valeur efficace de la difference entre deux echantillons, soit
// les aigus) sert en FM : sans signal, le discriminateur sort un souffle fort
// qui baisse quand une porteuse arrive.
// Usage : md_level -r <frequence> [-c <canaux>] [-m fm]

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LEVEL_FILE "/dev/shm/multidec/level"

int main(int argc, char **argv)
{
  int rate = 22050, ch = 1, i;
  int16_t buf[4096];
  double sum = 0, dsum = 0;
  long n = 0, per;
  int16_t prev = 0;

  for (i = 1; i < argc - 1; i++)
  {
    if (!strcmp(argv[i], "-r")) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-c")) ch = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-m")) i++;          // fm : meme calcul, lu par multidec
  }
  if (ch < 1) ch = 1;
  per = (long)rate * ch / 4;

  for (;;)
  {
    ssize_t r = read(0, buf, sizeof buf);
    ssize_t w = 0;
    if (r <= 0)
    {
      break;
    }
    while (w < r)
    {
      ssize_t k = write(1, (char *)buf + w, r - w);
      if (k <= 0) return 0;
      w += k;
    }
    for (i = 0; i < r / 2; i++)
    {
      double v = buf[i] / 32768.0;
      double d = (buf[i] - prev) / 65536.0;
      if (ch == 1) prev = buf[i];
      sum += v * v;
      dsum += d * d;
      if (++n >= per)
      {
        char s[48];
        int fd;
        int len = snprintf(s, sizeof s, "%.1f %.1f\n", 10 * log10(sum / n + 1e-12), 10 * log10(dsum / n + 1e-12));
        fd = open(LEVEL_FILE ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
          if (write(fd, s, len) == len) rename(LEVEL_FILE ".tmp", LEVEL_FILE);
          close(fd);
        }
        sum = dsum = 0;
        n = 0;
      }
    }
  }
  return 0;
}
