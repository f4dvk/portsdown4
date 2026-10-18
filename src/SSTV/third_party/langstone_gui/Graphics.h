// Graphics.h
//
// Repris de Langstone-V2 (Colin Durbridge G4EML, GPLv3) :
// https://github.com/g4eml/Langstone-V2
//
// Modifications par rapport a l'original (voir ORIGIN.md) :
//  - setPixel() controlait les bornes en dur ("x<800 && y<480", la
//    resolution de l'ecran tactile 7" utilise par Langstone). Ici les
//    bornes utilisent screenXsize/screenYsize, lues dynamiquement sur
//    l'ecran reellement present, pour ne pas se limiter a une seule
//    resolution d'ecran.
//  - setPixel() ne supposait qu'un framebuffer 32 bits/pixel (BGRA). Le
//    support des framebuffers 16 bits/pixel (RGB565, frequent sur les
//    petits ecrans SPI/TFT type fbtft) et 24 bits/pixel a ete ajoute.
//  - initScreen() accepte desormais un chemin de peripherique et une
//    geometrie optionnelle (pour les tests hors Raspberry Pi, sans
//    /dev/fb0 reel).
//
// Le reste (dessin de texte, boutons, lignes) est inchange.

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include "Font.h"

char *fbp = 0;
int fbfd = 0;
long int screenSize = 0;
int screenXsize=0;
int screenYsize=0;
int screenBpp=32;
int currentX=0;
int currentY=0;
int textSize=1;
int foreColourR=0;
int foreColourG=0;
int foreColourB=0;
int backColourR=0;
int backColourG=0;
int backColourB=0;


void closeScreen(void);
int initScreen(void);
int initScreenAt(const char* device, int force_width, int force_height, int force_bpp);
void setPixel(int x, int y, int R, int G, int B);
void clearScreen();
void displayChar(int ch);
void setLargePixel(int x, int y, int size, int R, int G, int B);
void gotoXY(int x, int y);
void setForeColour(int R,int G,int B);
void setBackColour(int R,int G,int B);
void displayStr(char*s);
void displayButton(char*s);
void displayButton2x12(char*s1,char*s2);
void displayButton1x12(char*s1);
void drawLine(int x0, int y0, int x1, int y1,int r,int g,int b);

void displayStr(char*s)
{
 int p;
 p=0;
 do
 {
    displayChar(s[p++]);
 }
 while(s[p]!=0);
}

void displayChar(int ch)
{
  int row;
  int col;
  int pix;
  int descender;

  if(font[ch][0] & 0x80)
  {
  descender=3*textSize;
  }
  else
  {
  descender=0;
  }

  for(row=0;row<9;row++)
    {
    pix=font[ch][row];
    if(row==0) pix=pix & 0x7F;                //top bit of first row indicates descender
    for(col=0;col<8;col++)
      {
       if((pix << col) & 0x80)
         {
            setLargePixel(currentX+col*textSize,currentY+row*textSize+descender,textSize,foreColourR,foreColourG,foreColourB);
         }
       else
         {
            setLargePixel(currentX+col*textSize,currentY+row*textSize+descender,textSize,backColourR,backColourG,backColourB);
         }
      }
    }

  currentX=currentX+8*textSize;
}

void clearButton(void)
{
gotoXY(currentX+1,currentY+1);
for(int xi=0;xi<98;xi++)
  {
  for(int yi=0;yi<48;yi++)
    {
    setPixel(currentX+xi,currentY+yi,backColourR,backColourG,backColourB);
    }
  }

}

void displayButton(char*s)
{
int saveX=currentX;
int saveY=currentY;
for(int x=0;x<100;x++)
  {
    setPixel(currentX+x,currentY,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+x,currentY+50,foreColourR,foreColourG,foreColourB);
  }
for(int y=0;y<50;y++)
  {
    setPixel(currentX,currentY+y,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+100,currentY+y,foreColourR,foreColourG,foreColourB);
  }

gotoXY(saveX,saveY);
clearButton();
int sx=50-((strlen(s)*16)/2);
gotoXY(saveX+sx,saveY+18);
textSize=2;
displayStr(s);
currentX=saveX+105;
currentY=saveY;

}

void displayButton2x12(char*s1,char*s2)
{
int saveX=currentX;
int saveY=currentY;
for(int x=0;x<100;x++)
  {
    setPixel(currentX+x,currentY,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+x,currentY+50,foreColourR,foreColourG,foreColourB);
  }
for(int y=0;y<50;y++)
  {
    setPixel(currentX,currentY+y,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+100,currentY+y,foreColourR,foreColourG,foreColourB);
  }

gotoXY(saveX,saveY);
clearButton();
int sx=50-((strlen(s1)*8)/2);
gotoXY(saveX+sx,saveY+11);
textSize=1;
displayStr(s1);
sx=50-((strlen(s2)*8)/2);
gotoXY(saveX+sx,saveY+29);
textSize=1;
displayStr(s2);
currentX=saveX+105;
currentY=saveY;
}

void displayButton1x12(char*s1)
{
int saveX=currentX;
int saveY=currentY;
for(int x=0;x<100;x++)
  {
    setPixel(currentX+x,currentY,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+x,currentY+50,foreColourR,foreColourG,foreColourB);
  }
for(int y=0;y<50;y++)
  {
    setPixel(currentX,currentY+y,foreColourR,foreColourG,foreColourB);
    setPixel(currentX+100,currentY+y,foreColourR,foreColourG,foreColourB);
  }

gotoXY(saveX,saveY);
clearButton();
int sx=50-((strlen(s1)*8)/2);
gotoXY(saveX+sx,saveY+20);
textSize=1;
displayStr(s1);
currentX=saveX+105;
currentY=saveY;
}



void gotoXY(int x, int y)
{
currentX=x;
currentY=y;
}

void setForeColour(int R,int G,int B)
{
foreColourR=R;
foreColourG=G;
foreColourB=B;
}

void setBackColour(int R,int G,int B)
{
backColourR=R;
backColourG=G;
backColourB=B;
}

void clearScreen()
{
  for(int y=0;y<screenYsize;y++)
	  {
      for(int x=0;x<screenXsize;x++)
        {
        setPixel(x,y,backColourR,backColourG,backColourB);
        }
  	}
}

void setPixel(int x, int y, int R, int G, int B)
{
  if(x<0 || y<0 || x>=screenXsize || y>=screenYsize) return;

  long p = (long)(x + screenXsize*y) * (screenBpp/8);

  if(screenBpp==32)
  {
    memset(fbp+p,B,1);    //Blue
    memset(fbp+p+1,G,1);  //Green
    memset(fbp+p+2,R,1);  //Red
    memset(fbp+p+3,0x80,1); //A
  }
  else if(screenBpp==24)
  {
    memset(fbp+p,B,1);
    memset(fbp+p+1,G,1);
    memset(fbp+p+2,R,1);
  }
  else if(screenBpp==16)
  {
    // RGB565, octet de poids faible en premier
    uint16_t v = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
    fbp[p]   = (char)(v & 0xFF);
    fbp[p+1] = (char)(v >> 8);
  }
}

void drawLine(int x0, int y0, int x1, int y1,int r,int g,int b) {

  int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
  int dy = abs(y1-y0), sy = y0<y1 ? 1 : -1;
  int err = (dx>dy ? dx : -dy)/2, e2;

  for(;;){
    setPixel(x0,y0,r,g,b);
    if (x0==x1 && y0==y1) break;
    e2 = err;
    if (e2 >-dx) { err -= dy; x0 += sx; }
    if (e2 < dy) { err += dx; y0 += sy; }
  }
}

void setLargePixel(int x, int y, int size, int R, int G, int B)
{
  for (int px=0;px<size;px++)
    {
      for(int py=0;py<size;py++)
        {
        setPixel(x+px,y+py,R,G,B);
        }
    }
}

void closeScreen(void)
{
  munmap(fbp, screenSize);
  close(fbfd);
}

// Ouvre le framebuffer et lit sa geometrie via ioctl (cas normal, sur un
// vrai peripherique /dev/fbN). Si force_width/force_height/force_bpp sont
// non nuls, la geometrie est imposee sans passer par ioctl : cela permet
// d'utiliser un simple fichier comme framebuffer de test (pratique pour
// developper/valider le rendu sans materiel Raspberry Pi).
int initScreenAt(const char* device, int force_width, int force_height, int force_bpp)
{
  fbfd = open(device, O_RDWR | O_CREAT, 0644);
  if (fbfd < 0)
  {
    printf("Error: cannot open framebuffer device %s.\n", device);
    return(0);
  }

  if (force_width > 0 && force_height > 0 && force_bpp > 0)
  {
    screenXsize = force_width;
    screenYsize = force_height;
    screenBpp = force_bpp;
    screenSize = (long)screenXsize * screenYsize * (screenBpp/8);
    if (ftruncate(fbfd, screenSize) != 0)
    {
      printf("Error: cannot size framebuffer file %s.\n", device);
      return(0);
    }
  }
  else
  {
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo))
    {
      printf("Error reading fixed information.\n");
      return(0);
    }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo))
    {
      printf("Error reading variable information.\n");
      return(0);
    }

    screenXsize = vinfo.xres;
    screenYsize = vinfo.yres;
    screenBpp = vinfo.bits_per_pixel;
    screenSize = finfo.smem_len;
  }

  fbp = (char*)mmap(0, screenSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

  if (fbp == MAP_FAILED)
  {
    return 0;
  }
  else
  {
    return 1;
  }
}

int initScreen(void)
{
  return initScreenAt("/dev/fb0", 0, 0, 0);
}
