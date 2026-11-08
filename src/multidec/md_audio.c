// md_audio.c : sortie son de Portsdown. Recopie le son recu sur stdin
// (echantillons 16 bits signes, entrelaces) vers :
//   - le haut-parleur du Pi (aplay ; -s 0 : non). Touche Audio du decodeur
//     multiple : $MD_SHM/mute present, il recoit du silence (aplay garde son
//     rythme) ;
//   - la carte son virtuelle (plughw:Loopback,0,2 par aplay2, comme le
//     recepteur RTL-FM ; -l 0 : non) : tout ce qui y est joue part vers la
//     page web /touchscreen/ ;
//   - avec -w, les pages web qui ecoutent : port 2007 local, relaye par nginx
//     sur /touchscreen/audio. C'est le role de server/stream.sh : arecord lit
//     la carte virtuelle et la passe a "md_audio -w -s 0 -l 0".
// Usage : md_audio -r <frequence> [-c <canaux>] [-w] [-s 0] [-l 0]
//
// Flux web : reponse HTTP sans fin, echantillons bruts, en-tetes X-Rate et
// X-Channels. La source n'attend jamais le reseau : un auditeur trop lent est
// deconnecte (sa page se reconnecte d'elle-meme).

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PORT 2007
#define MAXC 8

static int clients[MAXC], nclients;

// Port d'ecoute local ; -1 s'il est encore pris (instance precedente en fin)
static int try_listen(void)
{
  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  struct sockaddr_in a;
  if (s < 0)
  {
    return -1;
  }
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if ((bind(s, (struct sockaddr *)&a, sizeof a) != 0) || (listen(s, 4) != 0))
  {
    close(s);
    return -1;
  }
  fcntl(s, F_SETFL, O_NONBLOCK);
  return s;
}

static void drop(int i)
{
  close(clients[i]);
  clients[i] = clients[--nclients];
}

static void accept_client(int ls, int rate, int ch)
{
  char h[256], req[2048];
  int c = accept(ls, NULL, NULL), one = 1, big = 256 * 1024;
  struct timeval tv = {0, 50000};
  if (c < 0)
  {
    return;
  }
  if (nclients >= MAXC)
  {
    close(c);
    return;
  }
  // la requete (GET ...) est lue sans etre analysee : un seul flux
  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  if (recv(c, req, sizeof req, 0) < 0)
  {
    // rien recu en 50 ms : on repond quand meme
  }
  snprintf(h, sizeof h, "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\n"
           "X-Rate: %d\r\nX-Channels: %d\r\n\r\n", rate, ch);
  if (send(c, h, strlen(h), MSG_NOSIGNAL) != (ssize_t)strlen(h))
  {
    close(c);
    return;
  }
  setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  setsockopt(c, SOL_SOCKET, SO_SNDBUF, &big, sizeof big);
  fcntl(c, F_SETFL, O_NONBLOCK);
  clients[nclients++] = c;
}

// Trames entieres seulement ; un envoi partiel decalerait les echantillons
// de cet auditeur : il est deconnecte
static void send_all(const unsigned char *b, size_t n)
{
  int i;
  for (i = 0; i < nclients; i++)
  {
    ssize_t w = send(clients[i], b, n, MSG_NOSIGNAL | MSG_DONTWAIT);
    if ((w < 0) ? ((errno != EAGAIN) && (errno != EWOULDBLOCK)) : ((size_t)w != n))
    {
      drop(i--);
    }
  }
}

int main(int argc, char **argv)
{
  static unsigned char buf[16384], zero[16384];
  char mute[160];
  int rate = 48000, ch = 1, i, ls = -1, ap = -1, lp = -1, web = 0, spk = 1, loop = 1;
  size_t keep = 0, frame;
  time_t lastbind = 0;
  char cmd[128];
  FILE *app = NULL, *lpp = NULL;
  for (i = 1; i < argc - 1; i++)
  {
    if (!strcmp(argv[i], "-r")) rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-c")) ch = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-s")) spk = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-l")) loop = atoi(argv[++i]);
  }
  for (i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-w")) web = 1;
  }
  if ((ch < 1) || (ch > 2)) ch = 1;
  frame = 2 * ch;
  snprintf(mute, sizeof mute, "%s/mute", getenv("MD_SHM") ? getenv("MD_SHM") : "/dev/shm/multidec");
  signal(SIGPIPE, SIG_IGN);
  if (spk)
  {
    snprintf(cmd, sizeof cmd, "aplay -q -t raw -r %d -f S16_LE -c %d 2>/dev/null", rate, ch);
    app = popen(cmd, "w");
    if (app) ap = fileno(app);
  }
  if (loop)
  {
    snprintf(cmd, sizeof cmd, "aplay2 -q -t raw -r %d -f S16_LE -c %d -D plughw:Loopback,0,2 2>/dev/null", rate, ch);
    lpp = popen(cmd, "w");
    if (lpp) lp = fileno(lpp);
  }
  if (web)
  {
    ls = try_listen();
  }
  for (;;)
  {
    struct pollfd pf[2];
    int n = 1;
    if (web && (ls < 0) && (time(NULL) != lastbind))
    {
      lastbind = time(NULL);
      ls = try_listen();
    }
    pf[0].fd = 0;
    pf[0].events = POLLIN;
    if (ls >= 0)
    {
      pf[1].fd = ls;
      pf[1].events = POLLIN;
      n = 2;
    }
    if (poll(pf, n, 1000) < 0)
    {
      if (errno == EINTR) continue;
      break;
    }
    if ((n == 2) && (pf[1].revents & POLLIN))
    {
      accept_client(ls, rate, ch);
    }
    if (pf[0].revents & (POLLIN | POLLHUP))
    {
      ssize_t r;
      if (keep >= frame) keep = 0;         // jamais : reste d'une trame incomplete
      r = read(0, buf + keep, sizeof buf - frame);
      size_t tot, whole, w = 0;
      if (r <= 0)
      {
        break;
      }
      const unsigned char *hp = access(mute, F_OK) ? buf + keep : zero;   // coupe : silence
      while ((ap >= 0) && (w < (size_t)r))   // haut-parleur ; sans lui, le web continue
      {
        ssize_t k = write(ap, hp + w, r - w);
        if (k <= 0) { ap = -1; break; }
        w += k;
      }
      for (w = 0; (lp >= 0) && (w < (size_t)r);)   // carte virtuelle (page web)
      {
        ssize_t k = write(lp, buf + keep + w, r - w);
        if (k <= 0) { lp = -1; break; }
        w += k;
      }
      tot = keep + r;
      whole = tot - tot % frame;
      if (whole && nclients)
      {
        send_all(buf, whole);
      }
      keep = tot - whole;
      memmove(buf, buf + whole, keep);
    }
  }
  while (nclients)
  {
    drop(0);
  }
  if (app)
  {
    pclose(app);
  }
  if (lpp)
  {
    pclose(lpp);
  }
  return 0;
}
