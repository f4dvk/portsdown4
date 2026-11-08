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
//
// Sorties (aplay, aplay2) relancees chaque seconde si elles s'arretent : au
// changement de service DAB, l'aplay2 precedent tient encore un instant la
// carte virtuelle ("occupee"), le nouveau la reprend des qu'elle est libre.
// A l'arret (SIGTERM, fin de stdin), les aplay sont coupes sans vider leur
// tampon : la carte est rendue tout de suite au lecteur suivant.

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
#include <sys/wait.h>
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

// Sortie vers un aplay : processus fils, tube, relance chaque seconde
typedef struct {
  char *argv[16];
  pid_t pid;
  int fd;
  time_t retry;
} output;

static void out_init(output *o, const char *prog, int rate, int ch, const char *dev)
{
  static char rs[16], cs[16];
  int n = 0;
  snprintf(rs, sizeof rs, "%d", rate);
  snprintf(cs, sizeof cs, "%d", ch);
  o->argv[n++] = (char *)prog;
  o->argv[n++] = "-q"; o->argv[n++] = "-t"; o->argv[n++] = "raw";
  o->argv[n++] = "-r"; o->argv[n++] = rs; o->argv[n++] = "-f"; o->argv[n++] = "S16_LE";
  o->argv[n++] = "-c"; o->argv[n++] = cs;
  if (dev) { o->argv[n++] = "-D"; o->argv[n++] = (char *)dev; }
  o->argv[n] = NULL;
  o->pid = -1;
  o->fd = -1;
  o->retry = 0;
}

// Arret immediat (SIGKILL) avant de fermer le tube : sur fin de flux ou
// SIGTERM, aplay vide d'abord son tampon (snd_pcm_drain : essai, 0,5 a plus de
// 3 s, haut-parleur comme carte virtuelle) et le lecteur suivant attend ou
// trouve la carte occupee. Sans risque pour dmix : ALSA prend son verrou
// partage avec SEM_UNDO, libere par le noyau a la mort du processus.
static void out_stop(output *o)
{
  if (o->pid > 0) kill(o->pid, SIGKILL);
  if (o->fd >= 0) close(o->fd);
  if (o->pid > 0) waitpid(o->pid, NULL, 0);
  o->fd = -1;
  o->pid = -1;
}

static void out_start(output *o)
{
  int p[2];
  o->retry = time(NULL) + 1;
  if (pipe(p)) return;
  o->pid = fork();
  if (o->pid == 0)
  {
    int fd = open("/dev/null", O_WRONLY);
    dup2(p[0], 0);
    if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
    close(p[0]);
    close(p[1]);
    execvp(o->argv[0], o->argv);
    _exit(127);
  }
  close(p[0]);
  if (o->pid < 0) { close(p[1]); return; }
  o->fd = p[1];
}

// Trames entieres ; sortie morte (aplay arrete, carte occupee) : relancee
static void out_write(output *o, const unsigned char *b, size_t n)
{
  size_t w = 0;
  if ((o->fd < 0) && (time(NULL) >= o->retry)) out_start(o);
  while ((o->fd >= 0) && (w < n))
  {
    ssize_t k = write(o->fd, b + w, n - w);
    if (k <= 0)
    {
      out_stop(o);
      o->retry = time(NULL) + 1;
      return;
    }
    w += k;
  }
}

static volatile sig_atomic_t quit;

static void on_term(int sig)
{
  (void)sig;
  quit = 1;
}

int main(int argc, char **argv)
{
  static unsigned char buf[16384], zero[16384];
  char mute[160];
  int rate = 48000, ch = 1, i, ls = -1, web = 0, spk = 1, loop = 1;
  size_t keep = 0, frame;
  time_t lastbind = 0;
  output hp, lb;
  struct sigaction sa;
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
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_term;                 // sans SA_RESTART : poll() rend la main
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  out_init(&hp, "aplay", rate, ch, NULL);                      // haut-parleur (carte par defaut)
  out_init(&lb, "aplay2", rate, ch, "plughw:Loopback,0,2");    // carte virtuelle (page web)
  if (web)
  {
    ls = try_listen();
  }
  while (!quit)
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
      size_t tot, whole;
      if (keep >= frame) keep = 0;         // jamais : reste d'une trame incomplete
      r = read(0, buf + keep, sizeof buf - frame);
      if (r <= 0)
      {
        break;
      }
      tot = keep + r;
      whole = tot - tot % frame;
      if (whole)
      {
        if (spk) out_write(&hp, access(mute, F_OK) ? buf : zero, whole);   // coupe : silence
        if (loop) out_write(&lb, buf, whole);
        if (nclients) send_all(buf, whole);
      }
      keep = tot - whole;
      memmove(buf, buf + whole, keep);
    }
  }
  while (nclients)
  {
    drop(0);
  }
  out_stop(&hp);
  out_stop(&lb);
  return 0;
}
