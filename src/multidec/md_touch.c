// md_touch.c : ecran tactile et commande web (port 2005) du decodeur multiple.
// Meme lecture des evenements que rtlsdrview.c (ecran 7 pouces officiel,
// DFRobot 5 pouces) ; les clics de la page web arrivent par FastCGI
// (touchscreenClick, comme dans les autres applications Portsdown).
// Chaque appui est ecrit dans un tube : deux int (x, y), origine en haut a
// gauche de l'ecran.

#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "ffunc.h"
#include "md.h"

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x) ((x) % BITS_PER_LONG)
#define LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

static int tfd = -1;                    // peripherique tactile
static int evpipe[2];
static float scale_x = 1, scale_y = 1;
static char display[32] = "Element14_7";
static bool webcontrol;

static void push(int x, int y)
{
  int ev[2];
  ev[0] = (x < 0) ? 0 : ((x >= MD_W) ? MD_W - 1 : x);
  ev[1] = (y < 0) ? 0 : ((y >= MD_H) ? MD_H - 1 : y);
  if (write(evpipe[1], ev, sizeof ev) < 0)
  {
    perror("touch");
  }
}

// Vrai si le peripherique ouvert est un ecran tactile ; bornes des axes
static bool touch_details(int *xmin, int *xmax, int *ymin, int *ymax)
{
  unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
  struct input_absinfo abs;
  bool touch = false;

  memset(bit, 0, sizeof bit);
  ioctl(tfd, EVIOCGBIT(0, EV_MAX), bit[0]);
  if (test_bit(EV_KEY, bit[0]))
  {
    ioctl(tfd, EVIOCGBIT(EV_KEY, KEY_MAX), bit[EV_KEY]);
    touch = test_bit(BTN_TOUCH, bit[EV_KEY]);
  }
  if (touch && test_bit(EV_ABS, bit[0]))
  {
    if (ioctl(tfd, EVIOCGABS(ABS_X), &abs) == 0) { *xmin = abs.minimum; *xmax = abs.maximum; }
    if (ioctl(tfd, EVIOCGABS(ABS_Y), &abs) == 0) { *ymin = abs.minimum; *ymax = abs.maximum; }
  }
  return touch;
}

static void *touch_thread(void *arg)
{
  struct input_event ev[64];
  int rawx = -1, rawy = -1;
  bool dfrobot = (strcmp(display, "dfrobot5") == 0);
  bool started = false;
  (void)arg;

  while (true)
  {
    ssize_t rb = read(tfd, ev, sizeof ev);
    int i;
    if (rb <= 0)
    {
      usleep(100000);
      continue;
    }
    if (!dfrobot)
    {
      rawx = -1;
      rawy = -1;
      started = false;
    }
    for (i = 0; i < (int)(rb / sizeof(struct input_event)); i++)
    {
      if ((ev[i].type == EV_KEY) && (ev[i].code == BTN_TOUCH))
      {
        if (ev[i].value == 1)
        {
          started = true;
          if (dfrobot) { rawx = -1; rawy = -1; }
        }
        else if (dfrobot && started && (rawx >= 0) && (rawy >= 0))
        {
          // DFRobot : position prise a la fin de l'appui
          push(rawx / scale_x, rawy / scale_y);
          started = false;
        }
      }
      else if ((ev[i].type == EV_ABS) && (ev[i].code == ABS_X) && (ev[i].value > 0))
      {
        rawx = ev[i].value;
      }
      else if ((ev[i].type == EV_ABS) && (ev[i].code == ABS_Y) && (ev[i].value > 0))
      {
        rawy = ev[i].value;
      }
      if (!dfrobot && started && (rawx >= 0) && (rawy >= 0))
      {
        push(rawx / scale_x, rawy / scale_y);
        started = false;
      }
    }
  }
  return NULL;
}

// ---------------------------------------------------------------- page web

static void parse_click(char *q, int *x, int *y)
{
  char *tok, *save = NULL;
  for (tok = strtok_r(q, "&\n", &save); tok; tok = strtok_r(NULL, "&\n", &save))
  {
    if (!strncmp(tok, "x=", 2)) *x = atoi(tok + 2);
    if (!strncmp(tok, "y=", 2)) *y = atoi(tok + 2);
  }
}

FFUNC touchscreenClick(ffunc_session_t *session)
{
  ffunc_str_t payload;
  if (!webcontrol)
  {
    return;
  }
  if (ffunc_read_body(session, &payload))
  {
    char q[128];
    int x = -1, y = -1;
    ffunc_write_out(session, "Status: 200 OK\r\n");
    ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
    ffunc_write_out(session, "%s\n", "click received.");
    snprintf(q, sizeof q, "%.*s", (int)payload.len, payload.data ? payload.data : "");
    parse_click(q, &x, &y);
    if ((x >= 0) && (y >= 0))
    {
      push(x, y);
    }
  }
  else
  {
    ffunc_write_out(session, "Status: 400 Bad Request\r\n");
    ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
    ffunc_write_out(session, "%s\n", "payload not found.");
  }
}

static void *web_thread(void *arg)
{
  (void)arg;
  while (webcontrol)
  {
    ffunc_run("multidec");
    sleep(1);
  }
  return NULL;
}

int touch_init(bool *web)
{
  char v[64] = "";
  int n, xmin = 0, xmax = 799, ymin = 0, ymax = 479;
  pthread_t th;

  if (pipe(evpipe) != 0)
  {
    return -1;
  }
  pconf_get(MD_PCONFIG, "display", v, sizeof v);
  if (v[0])
  {
    snprintf(display, sizeof display, "%s", v);
  }
  v[0] = 0;
  pconf_get(MD_PCONFIG, "webcontrol", v, sizeof v);
  webcontrol = (strcmp(v, "enabled") == 0);
  *web = webcontrol;

  for (n = 0; n < 7; n++)
  {
    char dev[32];
    snprintf(dev, sizeof dev, "/dev/input/event%d", n);
    tfd = open(dev, O_RDONLY);
    if (tfd < 0)
    {
      continue;
    }
    if (touch_details(&xmin, &xmax, &ymin, &ymax))
    {
      break;
    }
    close(tfd);
    tfd = -1;
  }
  if (tfd >= 0)
  {
    scale_x = (float)(xmax - xmin) / MD_W;
    scale_y = (float)(ymax - ymin) / MD_H;
    if (scale_x <= 0) scale_x = 1;
    if (scale_y <= 0) scale_y = 1;
    pthread_create(&th, NULL, touch_thread, NULL);
    pthread_detach(th);
  }
  if (webcontrol)
  {
    pthread_create(&th, NULL, web_thread, NULL);
    pthread_detach(th);
  }
  return evpipe[0];
}
