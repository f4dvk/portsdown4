#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <linux/input.h>

int tfd;
int touchX=0;   // coordonnees BRUTES du controleur (voir touchToScreen())
int touchY=0;
// Plage annoncee par le pilote du controleur tactile (EVIOCGABS), lue a
// l'ouverture : 0 et 0 si inconnue.
int touchXmin=0, touchXmax=0, touchYmin=0, touchYmax=0;

int touchAvailable();
int getTouch();
int initTouch(char * tpath);
void touchToScreen(int screenW, int screenH, int *sx, int *sy);


int initTouch(char * tpath)
{
        if ((tfd = open(tpath, O_RDONLY)) < 0) {
                return 1;
        }
        struct input_absinfo abs;
        if (ioctl(tfd, EVIOCGABS(ABS_X), &abs) == 0) { touchXmin = abs.minimum; touchXmax = abs.maximum; }
        if (ioctl(tfd, EVIOCGABS(ABS_Y), &abs) == 0) { touchYmin = abs.minimum; touchYmax = abs.maximum; }
        return 0;
}

// Convertit la derniere position brute (touchX/touchY) en pixels ecran :
// le controleur tactile n'a pas forcement la resolution du framebuffer
// (ecran 1024x600 force en 800x480, controleur resistif 0-4095...). Meme
// principe que rpidatvtouch4.c (scaleXvalue = (max-min)/wscreen), mais
// avec l'origine en haut a gauche comme le framebuffer. Plage inconnue :
// coordonnees brutes, inchangees.
static int touchScaleAxis(int raw, int rmin, int rmax, int size)
{
        if (rmax <= rmin) return raw;
        long v = (long)(raw - rmin) * size / (rmax - rmin);
        if (v < 0) v = 0;
        if (v > size - 1) v = size - 1;
        return (int)v;
}

void touchToScreen(int screenW, int screenH, int *sx, int *sy)
{
        *sx = touchScaleAxis(touchX, touchXmin, touchXmax, screenW);
        *sy = touchScaleAxis(touchY, touchYmin, touchYmax, screenH);
}

//Returns 0 if no touch available. 1 if Touch start. 2 if touch end. 3 if touch move

int getTouch()
{
	int i;
  size_t rb;
  struct input_event ev[64];
  int retval;
  
  retval=0;
  if(touchAvailable())
    {
    rb=read(tfd,ev,sizeof(struct input_event)*64);
        for (i = 0;  i <  (rb / sizeof(struct input_event)); i++)
        {
              if (ev[i].type ==  EV_SYN) 
                {
                if(retval==0) retval=3;
                break;  //action
                }
              else if (ev[i].type == EV_KEY && ev[i].code == 330 && ev[i].value == 1) retval=1; //touch start
              else if (ev[i].type == EV_KEY && ev[i].code == 330 && ev[i].value == 0) retval=2; //touch finish
              else if (ev[i].type == EV_ABS && ev[i].code == 0 && ev[i].value > 0)
              {
			          touchX = ev[i].value;
		          }
                else if (ev[i].type == EV_ABS  && ev[i].code == 1 && ev[i].value > 0)
                {
			            touchY = ev[i].value;
		            }

	       }

    }

 return retval;
}


int touchAvailable()  
{
  struct timeval tv;
  fd_set fds;
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  FD_ZERO(&fds);
  FD_SET(tfd, &fds);
  select(tfd+1, &fds, NULL, NULL, &tv);
  return (FD_ISSET(tfd, &fds));
}