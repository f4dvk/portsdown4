
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <iio.h>
#include <unistd.h>
#include <wiringPi.h>
#include <stdio.h>
#include <string.h>
#include "Graphics.h"
#include "Touch.h"
#include "Mouse.h"
#include "mcp23017.c"
#include "Morse.c"
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "/home/pi/rpidatv/src/gui/ffunc.h"

const char *i2cNode1 = "/dev/i2c-1";        //linux i2c node for the Hyperpixel display on Pi 4
const char *i2cNode2 = "/dev/i2c-11";       //linux i2c node for the Hyperpixel display on Pi 4
int  mcp23017_addr = 0x20;	               //MCP23017 I2C Address

void setFreq(double fr);
void displayFreq(double fr);
void setFreqInc();
void setMode(int mode);
void setVolume(int vol);
void setSquelch(int sql);
void setRxFilter(int low,int high);
void setBandBits(int b);
void processTouch();
void processMouse(int mbut);
void *WebClickListener(void * arg);
void parseClickQuerystring(char *query_string, int *x_ptr, int *y_ptr);
FFUNC touchscreenClick(ffunc_session_t * session);
void togglewebcontrol();
void initGUI();
void sendFifo(char * s);
void initFifos();
void initUDP(void);
void setRtlSdrRxFreq(long long rxfreq);
void setHwRxFreq(double fr);
void detectHw();
int buttonTouched(int bx,int by);
void displayMenu(void);
void displaySetting(int se);
void changeSetting(void);
void processGPIO(void);
void initGPIO(void);
void initMCP23017(int add);
int readConfig(void);
int writeConfig(void);
void initSDR(void);
void waterfall(void);
void clearWaterfall(void);
void P_Meter(void);
void S_Meter(void);
void setRit(int rit);
void setInputMode(int n);
void gen_palette(char colours[][3],int num_grads);
void setBand(int b);
long long runTimeMs(void);
void clearPopUp(void);
void displayPopupMode(void);
void displayPopupBand(void);
void displayError(char*st);
void flushUDP(void);
void setFFTBW(int bw);

void setDialLock(int d);
int firstpass=1;
double freq;
double freqInc=0.001;
#define numband 24
int band=3;
char ProgramName[255];         // used to pass rpidatvgui char string to listener
int *web_x_ptr;                // pointer
int *web_y_ptr;                // pointer
int web_x;                     // click x 0 - 799 from left
int web_y;                     // click y 0 - 480 from top
char WebClickForAction[7] = "no";  // no/yes
bool mouse_active = false;             // set true after first movement of mouse
bool MouseClickForAction = false;      // set true on left click of mouse
bool MouseClickRight = false;
int mouse_x;                           // click x 0 - 799 from left
int mouse_y;                           // click y 0 - 479 from top
bool image_complete = true;            // prevents mouse image buffer from being copied until image is complete
bool mouse_connected = false;          // Set true if mouse detected at startup
pthread_t thwebclick;          //  Listens for clicks from web interface
pthread_t thmouse;          //  Listens to the mouse

int wscreen, hscreen;
float scaleXvalue = 1;
float scaleYvalue = 1;

int CheckMouse();
void *WaitMouseEvent(void * arg);
void handle_mouse();
int scroll_change = 0;

double bandFreq[numband] = {28.500,50.200,70.200,144.200,432.200,1296.200,2320.200,2400.100,3400.100,5760.100,10368.200,24048.200,10489.55,433.2,433.2,433.2,433.2,433.2,433.2,1296.2,1296.2,1296.2,1296.2,1296.2};
double bandRxOffset[numband]={0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,-5328.0,-9936.0,-23616.0,-10345.0,0,0,0,0,0,0,0,0,0,0,0,0};
int bandRxHarmonic[numband]={1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
int bandMode[numband]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int bandBitsRx[numband]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23};
int bandSquelch[numband]={30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30,30};
int bandFFTRef[numband]={-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10,-10};
float bandSmeterZero[numband]={-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80,-80};
int bandSSBFiltLow[numband]={300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300,300};
int bandSSBFiltHigh[numband]={3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000,3000};
int bandFFTBW[numband]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

#define minHwFreq 24.000000
#define maxHwFreq 1750.000000

#define nummode 7
int mode=0;
int lastmode=0;
char * modename[nummode]={"USB","LSB","CW ","CWN","FM ","AM ","DMR"};
enum {USB,LSB,CW,CWN,FM,AM,DMR};

#define numSettings 8

char * settingText[numSettings]={" Rx Offset= ","Rx Harmonic Mixing= ","Band Bits (Rx)= ","FFT Ref= ","S-Meter Zero= ", "SSB Rx Filter Low= ", "SSB Rx Filter High= ", "24 Bands= "};
enum {RX_OFFSET,RX_HARMONIC,BAND_BITS_RX,FFT_REF,S_ZERO,SSB_FILT_LOW,SSB_FILT_HIGH,BANDS24};
int settingNo=RX_OFFSET;
int setIndex=0;
int maxSetIndex=10;

enum {FREQ,SETTINGS,VOLUME,SQUELCH,RIT};
int inputMode=FREQ;

//GUI Layout values X and Y coordinates for each group of buttons.

#define downButtonX 30
#define downButtonY 175
#define upButtonX 30
#define upButtonY 120
#define volButtonX 660
#define volButtonY 300
#define sqlButtonX 30
#define sqlButtonY 300
#define ritButtonX 670
#define ritButtonY 60
#define funcButtonsY 429
#define funcButtonsX 30
#define buttonHeight 50
#define buttonSpaceY 55
#define buttonWidth 100
#define buttonSpaceX 105
#define freqDisplayX 80
#define freqDisplayY 55
#define freqDisplayCharWidth 48
#define freqDisplayCharHeight 54
#define txX 600
#define txY 15
#define modeX 200
#define modeY 15
#define moniX 500
#define moniY 15
#define settingX 200
#define settingY 390
#define popupX 30
#define popupY 374
#define FFTX 140
#define FFTY 216
#define sMeterX 2
#define sMeterY 5
#define sMeterWidth 170
#define sMeterHeight 40
#define errorX 200
#define errorY 240
#define diallockX 316
#define diallockY 15

int moni=0;
int fifofd;
int dotCount=0;
int transmitting=0;
int dialLock=0;

int rxFilterLow;
int rxFilterHigh;

int bands24 = 0;

int keyDownTimer=0;


#define configDelay 500                              //delay before config is written after tuning (5 Seconds)
int configCounter=configDelay;

int twoButTimer=0;
int lastBut=0;

long long lastLOhz;

long long lastClock;
long progStartTime=0;

int lastKey=1;

int sMeterType = 0;

int volume=20;
#define maxvol 100

int squelch=20;
#define maxsql 100

int rit=0;
#define minrit -9990
#define maxrit 9990

int TxAtt=0;

int tuneDigit=8;
#define maxTuneDigit 11

#define RXDELAY 10000       //10ms delay between sending rx command to SDR and setting Tx output bit low.

#define BurstLength 500000     //length of 1750Hz Burst   500ms

char mousePath[20];
char touchPath[20];
int mousePresent;
int touchPresent;
int portsdownPresent;
int hyperPixelPresent;
int MCP23017Present;

int popupSel=0;
int popupFirstBand;
enum {NONE,MODE,BAND};

#define keyPin 1        //Wiring Pi pin number. Physical pin is 12
#define txPin 29        //Wiring Pi pin number. Physical pin is 40
#define bandPin1 31     //Wiring Pi pin number. Physical pin is 28
#define bandPin1alt 26  //Wiring Pi pin number. Physical pin is 32     Bandpin1 is copied to both of these pins to retain compatibility with Portsdown.
#define bandPin2 24     //Wiring Pi pin number. Physical pin is 35
#define bandPin3 7      //Wiring Pi pin number. Physical pin is 7
#define bandPin4 6      //Wiring Pi pin number. Physical pin is 22
#define bandPin5 4      //Wiring Pi pin number. Physical pin is 16
#define bandPin6 5      //Wiring Pi pin number. Physical pin is 18
#define bandPin7 12     //Wiring Pi pin number. Physical pin is 19
#define bandPin8 13     //Wiring Pi pin number. Physical pin is 21

#define i2cKeyPin 1     //MCP23017 Key Input if fitted  Port A bit 1
#define i2cTxPin 7      //MCP23017 TX Output if fitted
                        //MCP23017 uses port B for band bits.


//robs Waterfall

float inbuf[2];
FILE *fftstream;
FILE *txfftstream;
float buf[512][130];
int points=512;
int rows=130;
int FFTRef = -30;
int spectrum_rows=80;
unsigned char * palette;
int HzPerBin=94;                        //calculated from FFT width and number of samples. Width=48000 number of samples =512
int bwBarStart=3;
int bwBarEnd=34;
float sMeter;                             //peak reading S meter.
float sMeterPeak;


//UDP server to receive FFT data from GNU RAdio

#define RXPORT 7373
#define TXPORT 7474




int main(int argc, char* argv[])
{
  strcpy(ProgramName, argv[0]);
  wscreen = 800;
  hscreen = 480;
  initUDP();
  lastClock=0;
  readConfig();
  detectHw();
  initFifos();
  initScreen();
  initGPIO();
  if(touchPresent)
  {
    initTouch(touchPath);
  }
  else
  {
    handle_mouse();
  }
  if((mousePresent) && (touchPresent)) initMouse(mousePath);
  initGUI();
  initSDR();
  togglewebcontrol();
  //              RGB Vals   Black >  Blue  >  Green  >  Yellow   >   Red     4 gradients    //number of gradients is varaible
  gen_palette((char [][3]){ {0,0,0},{0,0,255},{0,255,0},{255,255,0},{255,0,0}},4);

  refreshMouseBackground();

  while(1)
  {

    processGPIO();

    if(touchPresent)
      {
        if(getTouch()==1)
          {
            processTouch();
          }
      }

    if (strcmp(WebClickForAction, "yes") == 0)
      {
        touchX = (web_x * scaleXvalue);
        touchY = (web_y * scaleYvalue);
        strcpy(WebClickForAction, "no");
        fprintf(stderr,"Web touchX = %d, touchY = %d\n", touchX, touchY);
        processTouch();
      }
    else if (MouseClickForAction == true)
     {
       touchX = mouse_x;
       touchY = mouse_y;
       MouseClickForAction = false;
       printf("Mouse rawX = %d, rawY = %d\n", touchX, touchY);
       processTouch();
       refreshMouseBackground();
     }
   else if (scroll_change)
    {
      processMouse(128);
      scroll_change = 0;
      refreshMouseBackground();
    }
   else if (MouseClickRight)
    {
      processMouse(2+128);
      MouseClickRight = 0;
      refreshMouseBackground();
    }

    if ((mousePresent) && (touchPresent))
      {
        int but=getMouse();
        if(but>0)
          {
             processMouse(but);
          }
       if(twoButTimer>0)
       {
        twoButTimer--;
        if(twoButTimer==0)
          {
            lastBut=0;
          }
       }

      }

    waterfall();

    if(configCounter>0)                                       //save the config after 5 seconds of inactivity.
    {
      configCounter=configCounter-1;
      if(configCounter==0)
        {
        writeConfig();
        }
    }


    while(runTimeMs() < (lastClock + 10))                //delay until the next iteration at 100 per second (10ms)
    {
    usleep(100);
    }
    lastClock=runTimeMs();
  }
}

long long runTimeMs(void)
{
struct timeval tt;
gettimeofday(&tt,NULL);
if(progStartTime==0)
  {
    progStartTime=tt.tv_sec;
  }
tt.tv_sec=tt.tv_sec - progStartTime;
return ((tt.tv_sec*1000) + (tt.tv_usec/1000));
}

void gen_palette(char colours[][3], int num_grads){
  //allocate some memory, size of palette
  palette = malloc(sizeof(char)*256*3);

  int diff[3];
  float scale=256/(num_grads);
  int pos=0;

  for(int i=0;i<num_grads;i++){
      //get differences in colours for current gradient
      diff[0]=(colours[i+1][0]-colours[i][0])/(scale-1);
      diff[1]=(colours[i+1][1]-colours[i][1])/(scale-1);
      diff[2]=(colours[i+1][2]-colours[i][2])/(scale-1);

      //create the palette built up of multiple gradients
      for(int n=0;n<scale;n++){
          palette[pos*3+2]=colours[i][0]+(n*diff[0]);
          palette[pos*3+1]=colours[i][1]+(n*diff[1]);
          palette[pos*3]=colours[i][2]+(n*diff[2]);
          pos++;
      }

  }
}

void flushUDP(void)
{
int ret;
  do
  {
  ret = fread(&inbuf,sizeof(float),1,txfftstream);
  }
  while (ret>0);

  do
  {
  ret = fread(&inbuf,sizeof(float),1,fftstream);
  }
  while (ret>0);
}


void waterfall()
{
  int level,level2;
  int ret;
  int baselevel;
  int bwbaroffset;
  float scaling;
  int fftref;
  int centreShift=0;

  ret = fread(&inbuf,sizeof(float),1,fftstream);
  fftref=FFTRef;

      if(ret>0)
    {

        //shift buffer
        for(int r=rows-1;r>0;r--)
        {
          for(int p=0;p<points;p++)
          {
            buf[p][r]=buf[p][r-1];
          }
        }

        buf[0+points/2][0]=inbuf[0];    //use the read value

        //Read in float values, shift centre and store in buffer 1st 'row'
        for(int p=1;p<points;p++)
        {
          fread(&inbuf,sizeof(float),1,fftstream);

          if(p<points/2)
          {
            buf[p+points/2][0]=inbuf[0];
          }else
          {
            buf[p-points/2][0]=inbuf[0];
          }
        }

         bwbaroffset=0;

        //use raw values to calculate S meter value
        //use highest reading within the receiver bandwidth

         sMeterPeak=-200;
         for (int p=points/2+bwBarStart-bwbaroffset;p<points/2+bwBarEnd-bwbaroffset;p++)
          {
           if(buf[p][0]>sMeterPeak)
             {
             sMeterPeak=buf[p][0];
             }

           }


        //RF level adjustment


        baselevel=fftref-80;
        scaling = 255.0/(float)(fftref-baselevel);



        //draw waterfall
        for(int r=0;r<rows;r++)
        {
          for(int p=0;p<points;p++)
          {
            //limit values displayed to range specified
            if (buf[p][r]<baselevel) buf[p][r]=baselevel;
            if (buf[p][r]>fftref) buf[p][r]=fftref;

            //scale to 0-255
            level = (buf[p][r]-baselevel)*scaling;
            setPixel(p+FFTX,FFTY+20+r,palette[level*3+2],palette[level*3+1],palette[level*3]);
          }
        }

        //clear spectrum area
        for(int r=0;r<spectrum_rows+1;r++)
        {
          for(int p=0;p<points;p++)
          {
            setPixel(p+FFTX,FFTY-r,0,0,0);
          }
        }

        //draw spectrum line

        scaling = spectrum_rows/(float)(fftref-baselevel);
        for(int p=0;p<points-1;p++)
        {
            //limit values displayed to range specified
            if (buf[p][0]<baselevel) buf[p][0]=baselevel;
            if (buf[p][0]>fftref) buf[p][0]=fftref;

            //scale to display height
            level = (buf[p][0]-baselevel)*scaling;
            level2 = (buf[p+1][0]-baselevel)*scaling;
            drawLine(p+FFTX, FFTY-level, p+1+FFTX, FFTY-level2,255,255,255);
        }

          //draw Bandwidth indicator
          int p=points/2;

          bwBarStart=rxFilterLow/HzPerBin;
          bwBarEnd=rxFilterHigh/HzPerBin;

          if(bwBarStart < -255) bwBarStart = -255;
          if(bwBarEnd  > 255) bwBarEnd = 255;

          centreShift=0;

          if(bwBarStart > -255) drawLine(p+FFTX+bwBarStart-bwbaroffset, FFTY-spectrum_rows+5, p+FFTX+bwBarStart-bwbaroffset, FFTY-spectrum_rows,255,140,0);
          drawLine(p+FFTX+bwBarStart-bwbaroffset, FFTY-spectrum_rows, p+FFTX+bwBarEnd-bwbaroffset, FFTY-spectrum_rows,255,140,0);

          if(bwBarEnd < 255) drawLine(p+FFTX+bwBarEnd-bwbaroffset, FFTY-spectrum_rows+5, p+FFTX+bwBarEnd-bwbaroffset, FFTY-spectrum_rows,255,140,0);
          //draw centre line (displayed frequency)
          drawLine(p+FFTX+centreShift, FFTY-10, p+FFTX+centreShift, FFTY-spectrum_rows,255,0,0);


          //draw x Axis.
          int ticks[11] = {0,21,43,64,85,107,128,149,171,192,213};    // Rounded tick spacing at 21.333 pixels per tick
          drawLine(FFTX,FFTY +3,FFTX + points,FFTY+3,0,255,0);
          for(int tick = 0;tick < 11;tick++)
            {
             drawLine(FFTX + p + ticks[tick],FFTY+3,FFTX + p + ticks[tick],FFTY +5,0,255,0);
             drawLine(FFTX + p - ticks[tick],FFTY+3,FFTX + p - ticks[tick],FFTY +5,0,255,0);
            }

         //draw scale
          setForeColour(0,255,0);
          textSize=1;
          gotoXY(p+FFTX-12,FFTY+8);
          displayStr(" 0 ");
          switch(bandFFTBW[band])
          {
              case 0:                 //48KHz BW
              gotoXY(p+FFTX-ticks[5]-24,FFTY+8);
              displayStr(" -10k   ");
              gotoXY(p+FFTX-ticks[10]-24,FFTY+8);
              displayStr(" -20k   ");
              gotoXY(p+FFTX+ticks[5]-24,FFTY+8);
              displayStr(" +10k   ");
              gotoXY(p+FFTX+ticks[10]-24,FFTY+8);
              displayStr(" +20k   ");
            break;
            case 1:                 //24KHz BW
              gotoXY(p+FFTX-ticks[5]-20,FFTY+8);
              displayStr(" -5k   ");
              gotoXY(p+FFTX-ticks[10]-24,FFTY+8);
              displayStr(" -10k   ");
              gotoXY(p+FFTX+ticks[5]-20,FFTY+8);
              displayStr(" +5k   ");
              gotoXY(p+FFTX+ticks[10]-24,FFTY+8);
              displayStr(" +10k   ");
            break;
            case 2:                 //12KHz BW
              gotoXY(p+FFTX-ticks[5]-28,FFTY+8);
              displayStr(" -2.5k   ");
              gotoXY(p+FFTX-ticks[10]-20,FFTY+8);
              displayStr(" -5k   ");
              gotoXY(p+FFTX+ticks[5]-28,FFTY+8);
              displayStr(" +2.5k   ");
              gotoXY(p+FFTX+ticks[10]-20,FFTY+8);
              displayStr(" +5k   ");
            break;
            case 3:                 //6KHz BW
              gotoXY(p+FFTX-ticks[5]-32,FFTY+8);
              displayStr(" -1.25k   ");
              gotoXY(p+FFTX-ticks[10]-28,FFTY+8);
              displayStr(" -2.5k   ");
              gotoXY(p+FFTX+ticks[5]-32,FFTY+8);
              displayStr(" +1.25k   ");
              gotoXY(p+FFTX+ticks[10]-28,FFTY+8);
              displayStr(" +2.5k   ");
            break;
          }


          if(transmitting==0)
          {
          S_Meter();
          }

          else
          {
          P_Meter();
          }
   }
}


void S_Meter(void)
{

          char smStr[10];
          static int sMeterCount=0;

          sMeterPeak=sMeterPeak-bandSmeterZero[band];                   //adjust offset to give positive values for s-meter
          int dbOver=0;
          int sValue=0;

          if(sMeterPeak < 0) sMeterPeak=0;
          if(sMeterPeak >= sMeter)
            {
            sMeter=sMeterPeak;                                    //fast attack
            }
          else
            {
            if(sMeter > 0) sMeter=sMeter-2;                    //slow decay
            }


          if(sMeter<55)
            {
            sValue=sMeter/6;
            }
          else
            {
            sValue=9;
            dbOver=sMeter-54;
            }



          //Draw S meter
          drawLine(sMeterX,sMeterY,sMeterX+sMeterWidth,sMeterY,255,255,255);
          drawLine(sMeterX,sMeterY,sMeterX,sMeterY+sMeterHeight,255,255,255);
          drawLine(sMeterX,sMeterY+sMeterHeight,sMeterX+sMeterWidth,sMeterY+sMeterHeight,255,255,255);
          drawLine(sMeterX+sMeterWidth,sMeterY,sMeterX+sMeterWidth,sMeterY+sMeterHeight,255,255,255);

          for(int ln=0;ln<10;ln++)
          {
          if(sMeter<55)
          {
          drawLine(sMeterX+5,sMeterY+5+ln,sMeterX+6+sMeter*2,sMeterY+5+ln,0,255,0);
          drawLine(sMeterX+6+sMeter*2,sMeterY+5+ln,sMeterX+6+160,sMeterY+5+ln,0,0,0);
          }
          else
          {
          int redbit=sMeterX+6+110+(sMeter-55)*2;
          if(redbit > (sMeterX+6+160)) redbit= sMeterX+6+160;
          drawLine(sMeterX+5,sMeterY+5+ln,sMeterX+6+110,sMeterY+5+ln,0,255,0);
          drawLine(sMeterX+6+110,sMeterY+5+ln,redbit,sMeterY+5+ln,255,0,0);
          drawLine(redbit,sMeterY+5+ln,sMeterX+6+160,sMeterY+5+ln,0,0,0);
          }

          }

          sMeterCount++;
          if(sMeterCount>5)
          {
              sMeterCount=0;
              textSize=2;
              setForeColour(0,255,0);
              gotoXY(sMeterX+10,sMeterY+20);
              if (sMeterType == 0)
              {
              sprintf(smStr,"S%d",sValue);
              displayStr(smStr);
              if(dbOver>0)
                {
                sprintf(smStr,"+%ddB  ",dbOver);
                displayStr(smStr);
                }
              else
                {
                displayStr("       ");
                }
              }
              else
              {
                sprintf(smStr,"%.0f dB    ",sMeter);
                displayStr(smStr);
              }
           }

}

 void P_Meter(void)
{

          char smStr[10];
          static int sMeterCount=0;

          sMeterPeak=(sMeterPeak+50)*2.1;

          if(sMeterPeak < 0) sMeterPeak=0;
          if(sMeterPeak >100) sMeterPeak=100;     // Now Scaled between 0 and 100






          if(sMeterPeak >= sMeter)
            {
            sMeter=sMeterPeak;                                    //fast attack
            }
          else
            {
            if(sMeter > 0)
             {
             if((mode==CW) || (mode==CWN))
                {
                sMeter=sMeter-10;                               //fast decay for CW
                }
              else if((mode==USB) || (mode==LSB))
                {
                sMeter=sMeter-3;                                 //Slow Decay for SSB.
                }
              else
                {
                sMeter=sMeter-0.5;                               //very slow decay
                }
              }

             }

             if(sMeter<0) sMeter=0;

          //Draw PO meter
          drawLine(sMeterX,sMeterY,sMeterX+sMeterWidth,sMeterY,255,255,255);
          drawLine(sMeterX,sMeterY,sMeterX,sMeterY+sMeterHeight,255,255,255);
          drawLine(sMeterX,sMeterY+sMeterHeight,sMeterX+sMeterWidth,sMeterY+sMeterHeight,255,255,255);
          drawLine(sMeterX+sMeterWidth,sMeterY,sMeterX+sMeterWidth,sMeterY+sMeterHeight,255,255,255);

          for(int ln=0;ln<10;ln++)
          {
          drawLine(sMeterX+5,sMeterY+5+ln,sMeterX+6+sMeter*1.5,sMeterY+5+ln,0,255,0);
          drawLine(sMeterX+6+sMeter*1.5,sMeterY+5+ln,sMeterX+6+160,sMeterY+5+ln,0,0,0);
          }

          sMeterCount++;
          if(sMeterCount>5)
          {
              sMeterCount=0;
              textSize=2;
              setForeColour(0,255,0);
              gotoXY(sMeterX+10,sMeterY+20);
              displayStr("Tx Level");
           }

}




void detectHw()
{
  FILE * fp;
  char * ln=NULL;
  size_t len=0;
  ssize_t rd;
  int p;
  char handler[2][10];
  char * found;
  p=0;
  mousePresent=0;
  touchPresent=0;
  portsdownPresent=0;
  fp=fopen("/proc/bus/input/devices","r");
   while ((rd=getline(&ln,&len,fp)!=-1))
    {
      if(ln[0]=='N')        //name of device
      {
        p=0;
        if((strstr(ln,"FT5406")!=NULL) || (strstr(ln,"pi-ts")!=NULL))         //Found Raspberry Pi TouchScreen entry
          {
           p=1;
           hyperPixelPresent=0;
          }
        if(strstr(ln,"MPI7003")!=NULL)
          {
           p=1;
           hyperPixelPresent=0;
           scaleXvalue = (1024 / wscreen);
           scaleYvalue = (600 / hscreen);
          }
        if(strstr(ln,"Goodix")!=NULL)                                         //Found Hyperpixel TouchScreen entry
          {
          p=1;
          hyperPixelPresent=1;
          }
      }

      if(ln[0]=='H')        //handlers
      {
         if(strstr(ln,"mouse")!=NULL)
         {
           found=strstr(ln,"event");
           strcpy(handler[p],found);
           handler[p][6]=0;
           if(p==0)
            {
              sprintf(mousePath,"/dev/input/%s",handler[0]);
              mousePresent=1;
            }
           if(p==1)
           {
             sprintf(touchPath,"/dev/input/%s",handler[1]);
             touchPresent=1;
           }
         }
      }
    }
  fclose(fp);
  if(ln)  free(ln);

  if ((fp = fopen("/home/pi/rpidatv/bin/rpidatvgui", "r")))                      //test to see if Portsdown file is present. If so we change the exit behaviour.
  {
    fclose(fp);
    portsdownPresent=1;
  }

  if ((fp = fopen("/home/pi/hyperpixel4/install.sh", "r")))                      //test to see if Hyperpixel4 file is present. If so we dont use the GPIO pins.
  {
    fclose(fp);
    hyperPixelPresent=1;
  }
  else
  {
    hyperPixelPresent=0;
  }


// try to initialise MCP23017 i2c chip for additonal I/O
// this will set or reset MCP23017Present flag
  initMCP23017(mcp23017_addr);
}

void displayError(char*st)
{
  gotoXY(errorX,errorY);
  setForeColour(255,0,0);
  textSize=2;
  displayStr(st);
}

void setRtlSdrRxFreq(long long rxfreq)
{
  char freqStr[12];
  sprintf(freqStr,"L%lld",rxfreq);
  sendFifo(freqStr);
}

void initUDP(void)
{
   struct sockaddr_in myaddr;
   int fdr;
   int fdt;

//initialise Receive UDP receiver for FFT Receiver stream
   fdr=socket(AF_INET,SOCK_DGRAM,0);
   memset((char *)&myaddr,0,sizeof(myaddr));                      //Set any valid address for receiving UDP packets
   myaddr.sin_family = AF_INET;                                  //Network Connection
   myaddr.sin_addr.s_addr = htonl(INADDR_ANY);                   //Any Address
   myaddr.sin_port = htons(RXPORT);                              //set UDP POrt to listen on
   bind(fdr,(struct sockaddr *)&myaddr,sizeof(myaddr));          //bind the socket to the address
   fftstream=fdopen(fdr,"r");                                    //open as a stream
   fcntl(fileno(fftstream), F_SETFL, O_RDONLY | O_NONBLOCK);    //set it as nonblocking

//initialise Receive UDP receiver for FFT Transmitter stream
   fdr=socket(AF_INET,SOCK_DGRAM,0);
   memset((char *)&myaddr,0,sizeof(myaddr));                      //Set any valid address for receiving UDP packets
   myaddr.sin_family = AF_INET;                                   //Network Connection
   myaddr.sin_addr.s_addr = htonl(INADDR_ANY);                   //Any Address
   myaddr.sin_port = htons(TXPORT);                               //set UDP POrt to listen on
   bind(fdr,(struct sockaddr *)&myaddr,sizeof(myaddr));          //bind the socket to the address
   txfftstream=fdopen(fdr,"r");                                  //open as a stream
   fcntl(fileno(txfftstream), F_SETFL, O_RDONLY | O_NONBLOCK);   //set it as nonblocking

}



void initFifos()
{
 if(access("/tmp/langstoneRx",F_OK)==-1)   //does fifo exist already?
    {
        mkfifo("/tmp/langstoneRx", 0666);
    }
}


void sendFifo(char * s)
{
  char fs[50];
  int ret;
  int retry;
  strcpy(fs,s);
  strcat(fs,"\n");
  fifofd=open("/tmp/langstoneRx",O_WRONLY|O_NONBLOCK);
  retry=0;
    do
     {
       ret=write(fifofd,fs,strlen(fs));
       delay(5);
       retry++;
     }
   while((ret==-1)&(retry<10));
  if(ret==-1)
    {
      displayError("Lang_RX.py Not Responding");
      printf("Erreur FIFO\n");
     }
  close(fifofd);
  delay(1);
}

void initGPIO(void)
{
  if(hyperPixelPresent==0)
  {
  wiringPiSetup();
  pinMode(keyPin,INPUT);
  pinMode(txPin,OUTPUT);
  pinMode(bandPin1,OUTPUT);
  pinMode(bandPin1alt,OUTPUT);
  pinMode(bandPin2,OUTPUT);
  pinMode(bandPin3,OUTPUT);
  pinMode(bandPin4,OUTPUT);
  pinMode(bandPin5,OUTPUT);
  pinMode(bandPin6,OUTPUT);
  pinMode(bandPin7,OUTPUT);
  pinMode(bandPin8,OUTPUT);
  digitalWrite(txPin,LOW);
  digitalWrite(bandPin1,LOW);
  digitalWrite(bandPin1alt,LOW);
  digitalWrite(bandPin2,LOW);
  digitalWrite(bandPin3,LOW);
  digitalWrite(bandPin4,LOW);
  digitalWrite(bandPin5,LOW);
  digitalWrite(bandPin6,LOW);
  digitalWrite(bandPin7,LOW);
  digitalWrite(bandPin8,LOW);
  lastKey=1;
  }
}

void processGPIO(void)
{
int p1=1;
int p2=1;
int k1=1;
int k2=1;

if(hyperPixelPresent==0)              //can only use GPIO if Hyperpixel Display is not fitted.
  {
    k1=digitalRead(keyPin);
  }

if(MCP23017Present==1)                //MCP23017 extender chip can be used with any display.
  {
    k2=mcp23017_readbit(mcp23017_addr,GPIOA,i2cKeyPin);
  }

   k1=k1 & k2;        //allow Key from either source

      if(k1!=lastKey)
    {
    lastKey=k1;
    }

}


// Configure the MCP23017 GPIO Extender if it is fitted :

void initMCP23017(int add)
{
 int resp;
 resp=i2c_init(i2cNode1);	               			//Initialize i2c node for a pi with normal display
 if(resp<0)                                   //not found, try for pi with Hypepixel4 display
 {
  resp=i2c_init(i2cNode2);	               		//Initialize i2c node fo4r a pi with normal display
   if(resp<0)
    {
     MCP23017Present=0;                        //neither found so disable it
     return;
    }
 }
 resp= mcp23017_readport(mcp23017_addr,GPIOA);      //dummy read to check if device is present
 if(resp<0)
 {
   MCP23017Present=0;
   return;
 }
 mcp23017_writereg(add,GPPU,GPIOA,0x03);      //Port A pullups enabled
 mcp23017_writereg(add,IODIR,GPIOB,0x00);      //Port B all outputs for Band Bits
 mcp23017_writeport(add,GPIOA,0);
 mcp23017_writeport(add,GPIOB,0);             //Zero all outputs
 MCP23017Present=1;
}

void sqlButton(int show)
{
 char sqlStr[5];
  gotoXY(sqlButtonX,sqlButtonY);
  if(show==1)
    {
      setForeColour(0,255,0);
    }
  else
    {
      setForeColour(0,0,0);
    }
  displayButton("SQL");
  textSize=2;
  gotoXY(sqlButtonX+30,sqlButtonY-25);
  displayStr("   ");
  gotoXY(sqlButtonX+30,sqlButtonY-25);
  sprintf(sqlStr,"%d",squelch);
  displayStr(sqlStr);
}

void ritButton(int show)
{
 char ritStr[5];
 int to;
  if(show==1)
    {
      setForeColour(0,255,0);
    }
  else
    {
      setForeColour(0,0,0);
      gotoXY(ritButtonX,ritButtonY+buttonSpaceY);
      displayButton("Zero");
    }
  gotoXY(ritButtonX,ritButtonY);
  displayButton("RIT");
  textSize=2;
  to=0;
  if(abs(rit)>0) to=8;
  if(abs(rit)>9) to=16;
  if(abs(rit)>99) to=24;
  if(abs(rit)>999) to=32;
  gotoXY(ritButtonX,ritButtonY-25);
  displayStr("         ");
  gotoXY(ritButtonX+38-to,ritButtonY-25);
  if (rit==0)
  {
  sprintf(ritStr,"0");
  }
  else
  {
    sprintf(ritStr,"%+d",rit);
  }
  displayStr(ritStr);

}

void initGUI()
{
  clearScreen();

// Down Button
  gotoXY(downButtonX,downButtonY);
  setForeColour(0,255,0);
  displayButton("Down");

// Up Button
  gotoXY(upButtonX,upButtonY);
  setForeColour(0,255,0);
  displayButton("Up");

// Volume Button
  gotoXY(volButtonX,volButtonY);
  setForeColour(0,255,0);
  displayButton("Vol");


 //bottom row of buttons
  displayMenu();
  setBand(band);

  if((mode==FM) || (mode==DMR))
    {
    sqlButton(1);
    }
  else
    {
    sqlButton(0);
    }

    clearWaterfall();

}


void clearWaterfall(void)
{
  for(int r=0;r<rows;r++)
   {
   for(int p=0;p<points;p++)
    {
    buf[p][r]=-100;
    }
   }
   flushUDP();
}


void initSDR(void)
{
  setBand(band);
  setMode(mode);
  setVolume(volume);
  setSquelch(squelch);
  setRit(0);
  setFreqInc();
  lastLOhz=0;
  setFreq(freq);
}

void displayMenu()
{
gotoXY(funcButtonsX,funcButtonsY);
  setForeColour(0,255,0);
  displayButton("BAND");
  displayButton("MODE");
  displayButton("    ");
  displayButton("SET");
}


void displayPopupMode(void)
{
clearPopUp();
gotoXY(popupX,popupY);
  setForeColour(0,255,0);
  for(int n=0;n<nummode;n++)
  {
    displayButton(modename[n]);
  }
popupSel=MODE;
}

void displayPopupBand(void)
{
char bstr[6];
int b;
clearPopUp();
gotoXY(popupX,popupY);
  setForeColour(0,255,0);
 displayButton("More..");
  for(int n=0;n<6;n++)
  {
    b=bandFreq[n+popupFirstBand];
    sprintf(bstr,"%d", b);
    displayButton(bstr);
  }
popupSel=BAND;
}

void clearPopUp(void)
{
for(int py=popupY;py<popupY+buttonHeight+1;py++)
{
  for(int px=0;px<800;px++)
  {
  setPixel(px,py,0,0,0);
  }
}
popupSel=NONE;
displayMenu();
}

void parseClickQuerystring(char *query_string, int *x_ptr, int *y_ptr)
{
  char *query_ptr = strdup(query_string),
  *tokens = query_ptr,
  *p = query_ptr;

  while ((p = strsep (&tokens, "&\n")))
  {
    char *var = strtok (p, "="),
         *val = NULL;
    if (var && (val = strtok (NULL, "=")))
    {
      if(strcmp("x", var) == 0)
      {
        *x_ptr = atoi(val);
      }
      else if(strcmp("y", var) == 0)
      {
        *y_ptr = atoi(val);
      }
    }
  }
}

FFUNC touchscreenClick(ffunc_session_t * session)
{
  ffunc_str_t payload;

  if(ffunc_read_body(session, &payload))
  {

    ffunc_write_out(session, "Status: 200 OK\r\n");
    ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
    ffunc_write_out(session, "%s\n", "click received.");
    //fprintf(stderr, "Received click POST: %s (%d)\n", payload.data?payload.data:"", payload.len);

    int x = -1;
    int y = -1;
    parseClickQuerystring(payload.data, &x, &y);
    //fprintf(stderr, "After Parse: x: %d, y: %d\n", x, y);

    if((x >= 0) && (y >= 0))
    {
      web_x = x;                 // web_x is a global int
      web_y = y;                 // web_y is a global int
      strcpy(WebClickForAction, "yes");
      //fprintf(stderr, "Web Click Event x: %d, y: %d\n", web_x, web_y);
    }
  }
  else
  {
    ffunc_write_out(session, "Status: 400 Bad Request\r\n");
    ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
    ffunc_write_out(session, "%s\n", "payload not found.");
  }
}

void togglewebcontrol()
{
  fprintf(stderr, "Creating thread as webclick listener is not running\n");
  pthread_create (&thwebclick, NULL, &WebClickListener, NULL);
  fprintf(stderr, "Created webclick listener thread\n");
}

void *WaitMouseEvent(void * arg)
{
  int x = 0;
  int y = 0;
  int scroll = 0;
  int fd;
  char command[20];
  char buffer[3];
  bool left_button_action = false;

  int number = 0;
  FILE *nb_event = popen("ls -l /dev/input/by-id/ | grep 'mouse' | grep 'event' | tail -c2", "r");

  if (!nb_event) { perror("popen"); exit(1); };
  fscanf(nb_event, " %d", &number);
  pclose(nb_event);

  strcpy(command, "/dev/input/event");
  sprintf(buffer, "%d", number);
  strcat(command, buffer);

  if ((fd = open(command, O_RDONLY)) < 0)
  {
    perror("evdev open");
    exit(1);
  }
  struct input_event ev;
  while(1)
  {
    read(fd, &ev, sizeof(struct input_event));
    if (ev.type == 2)  // EV_REL
    {
      if (ev.code == 0) // x
      {
        x = x + ev.value;
        if (x < 0)
        {
          x = 0;
        }
        if (x > 799)
        {
          x = 799;
        }
        //printf("value %d, type %d, code %d, x_pos %d, y_pos %d\n",ev.value,ev.type,ev.code, x, y);
        //printf("x_pos %d, y_pos %d\n", x, y);
        mouse_active = true;
        draw_cursor2(x, y);
      }
      else if (ev.code == 1) // y
      {
        y = y - ev.value;
        if (y < 0)
        {
          y = 0;
        }
        if (y > 479)
        {
          y = 479;
        }
        //printf("value %d, type %d, code %d, x_pos %d, y_pos %d\n",ev.value,ev.type,ev.code, x, y);
        //printf("x_pos %d, y_pos %d\n", x, y);
        mouse_active = true;
        while (image_complete == false)  // Wait for menu to be drawn
        {
          usleep(1000);
        }
        draw_cursor2(x, y);
      }
      else if (ev.code == 8) // scroll wheel
      {
        scroll = scroll + ev.value;
        mouseScroll = mouseScroll + ev.value;
        scroll_change = 1;
        //printf("value %d, type %d, code %d, scroll %d\n",ev.value,ev.type,ev.code, scroll);
      }
      else
      {
        //printf("value %d, type %d, code %d\n", ev.value, ev.type, ev.code);
      }
    }
    else if (ev.type == 4)  // EV_MSC
    {
      if (ev.code == 4) // ?
      {
        if (ev.value == 589825)
        {
          //printf("value %d, type %d, code %d, left mouse click \n", ev.value, ev.type, ev.code);
          //printf("Waiting for up or down signal\n");
          left_button_action = true;
        }
        if (ev.value == 589826)
        {
          printf("value %d, type %d, code %d, right mouse click \n", ev.value, ev.type, ev.code);
        }
      }
    }
    else if (ev.type == 1)
    {
      //printf("value %d, type %d, code %d\n", ev.value, ev.type, ev.code);
      if ((left_button_action == true) && (ev.code == 272) && (ev.value == 1) && (mouse_active == true))
      {
        mouse_x = x;
        mouse_y = 479 - y;
        MouseClickForAction = true;
      }
      else if ((ev.code == 273) && (ev.value == 1) && (mouse_active == true))
      {
        mouse_x = x;
        mouse_y = 479 - y;
        MouseClickRight = true;
      }
      left_button_action = false;
    }
    else
    {
      //printf("value %d, type %d, code %d\n", ev.value, ev.type, ev.code);
    }
  }
}

int CheckMouse()
{
  FILE *fp;
  char response_line[255];
  // Read the Webcam address if it is present
  fp = popen("ls -l /dev/input | grep 'mouse'", "r");
  if (fp == NULL)
  {
    printf("Failed to run command\n" );
    exit(1);
  }
  // Response is "crw-rw---- 1 root input 13, 32 Apr 29 17:02 mouse0" if present, null if not
  // So, if there is a response, return 0.
  /* Read the output a line at a time - output it. */
  while (fgets(response_line, 250, fp) != NULL)
  {
    if (strlen(response_line) > 1)
    {
      pclose(fp);
      return 0;
    }
  }
  pclose(fp);
  return 1;
}

void handle_mouse()
{
  // First check if mouse is connected
  if (CheckMouse() != 0)    // Mouse not connected
  {
    return;
  }
  mouse_connected = true;
  printf("Starting Mouse Thread\n");
  pthread_create (&thmouse, NULL, &WaitMouseEvent, NULL);
}

void *WebClickListener(void * arg)
{
  while (true)
  {
    ffunc_run(ProgramName);
  }
  fprintf(stderr, "Exiting WebClickListener\n");
  return NULL;
}

void processMouse(int mbut)
{
  if(mbut==128)       //scroll whell turned
    {
      if((inputMode==FREQ) && (dialLock==0))
      {
        freq=freq+(mouseScroll*freqInc);
        mouseScroll=0;
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) < minHwFreq) freq=(minHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) > maxHwFreq) freq=(maxHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        setFreq(freq);
        return;
      }

      if(mouseScroll>0) mouseScroll=1;                     //prevent large changes when adjusting.
      if(mouseScroll<0) mouseScroll=-1;

      if(inputMode==SETTINGS)
      {
        changeSetting();
        return;
      }
      if(inputMode==VOLUME)
      {
        volume=volume+mouseScroll;
        mouseScroll=0;
        if(volume < 0) volume=0;
        if(volume > maxvol) volume=maxvol;
        setVolume(volume);
        return;
      }
      if(inputMode==SQUELCH)
      {
        squelch=squelch+mouseScroll;
        mouseScroll=0;
        if(squelch < 0) squelch=0;
        if(squelch > maxsql) squelch=maxsql;
        bandSquelch[band]=squelch;
        setSquelch(squelch);
        return;
      }
      if(inputMode==RIT)
      {
        rit=rit+mouseScroll*10;
        mouseScroll=0;
        if(rit < minrit) rit=minrit;
        if(rit > maxrit) rit=maxrit;
        setRit(rit);
        return;
      }
    }

  if(mbut==1+128)      //Left Mouse Button down
    {

      if(settingNo==BAND_BITS_RX)
       {
         setIndex=setIndex-1;
         if(setIndex<0) setIndex=0;
         displaySetting(settingNo);
       }
      else
       {
        tuneDigit=tuneDigit-1;
        if(tuneDigit<0) tuneDigit=0;
        if(tuneDigit==5) tuneDigit=4;
        if(tuneDigit==9) tuneDigit=8;
        setFreqInc();
        setFreq(freq);
        twoButTimer=20;
        lastBut=lastBut | 1;
        if((inputMode==SETTINGS) && (settingNo==BAND_BITS_RX))
          {
          displaySetting(BAND_BITS_RX);
          }
       }
    }

  if(mbut==2+128)      //Right Mouse Button down
    {
      if(settingNo==BAND_BITS_RX)
       {
         setIndex=setIndex+1;
         if((setIndex>maxSetIndex)&&(!MouseClickRight)) setIndex=maxSetIndex;
         else if(setIndex>maxSetIndex) setIndex=0;
         displaySetting(settingNo);
       }
      else if(!MouseClickRight)
       {
         tuneDigit=tuneDigit+1;
         if(tuneDigit > maxTuneDigit) tuneDigit=maxTuneDigit;
         if(tuneDigit==5) tuneDigit=6;
         if(tuneDigit==9) tuneDigit=10;
         setFreqInc();
         setFreq(freq);
         twoButTimer=20;
         lastBut=lastBut | 2;
         if((inputMode==SETTINGS) && (settingNo==BAND_BITS_RX))
           {
             displaySetting(BAND_BITS_RX);
           }
        }
    }

  if((mbut==3+128) | (lastBut==3))       //Middle button down or both buttons within 100ms
     {
      if(dialLock==0)
        {
          setDialLock(1);
        }
      else
        {
         setDialLock(0);
        }
       lastBut=0;
     }

  if(mbut==4+128)      //Extra Button down
    {

    }

  if(mbut==5+128)      //Side Button down
    {

    }


}

void setFreqInc()
{
  if(tuneDigit==0) freqInc=10000.0;
  if(tuneDigit==1) freqInc=1000.0;
  if(tuneDigit==2) freqInc=100.0;
  if(tuneDigit==3) freqInc=10.0;
  if(tuneDigit==4) freqInc=1.0;
  if(tuneDigit==5) tuneDigit=6;
  if(tuneDigit==6) freqInc=0.1;
  if(tuneDigit==7) freqInc=0.01;
  if(tuneDigit==8) freqInc=0.001;
  if(tuneDigit==9) tuneDigit=10;
  if(tuneDigit==10) freqInc=0.0001;
  if(tuneDigit==11) freqInc=0.00001;
}



void processTouch()
{

touchX=touchX/scaleXvalue;
touchY=touchY/scaleYvalue;

if(hyperPixelPresent==1)
{
float tempX=touchX;
float tempY=touchY;
tempX=tempX*0.6;                    //convert 0-800 to 0-480
tempY=800-(tempY*1.6666);           //convert 480-0   to 0-800
touchX=tempY;                       //swap X and Y
touchY=tempX;
}

// Down Frequency
if(buttonTouched(downButtonX,downButtonY))    //Down
  {
    if((inputMode==FREQ) && (dialLock==0))
      {
        freq=freq-freqInc;
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) < minHwFreq) freq=(minHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) > maxHwFreq) freq=(maxHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        setFreq(freq);
        return;
      }

    if(inputMode==SETTINGS)
      {
        changeSetting();
        return;
      }
    if(inputMode==VOLUME)
      {
        volume=volume-1;
        if(volume < 0) volume=0;
        if(volume > maxvol) volume=maxvol;
        setVolume(volume);
        return;
      }
    if(inputMode==SQUELCH)
      {
        squelch=squelch-1;
        if(squelch < 0) squelch=0;
        if(squelch > maxsql) squelch=maxsql;
        bandSquelch[band]=squelch;
        setSquelch(squelch);
        return;
      }
    if(inputMode==RIT)
      {
        rit=rit*-10;
        if(rit < minrit) rit=minrit;
        if(rit > maxrit) rit=maxrit;
        setRit(rit);
        return;
      }
  }

// Up Frequency
if(buttonTouched(upButtonX,upButtonY))    //up
  {
    if((inputMode==FREQ) && (dialLock==0))
      {
        freq=freq+freqInc;
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) < minHwFreq) freq=(minHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        if(((freq + bandRxOffset[band])/bandRxHarmonic[band]) > maxHwFreq) freq=(maxHwFreq - bandRxOffset[band])/bandRxHarmonic[band];
        setFreq(freq);
        return;
      }

    if(inputMode==SETTINGS)
      {
        changeSetting();
        return;
      }
    if(inputMode==VOLUME)
      {
        volume=volume+1;
        if(volume < 0) volume=0;
        if(volume > maxvol) volume=maxvol;
        setVolume(volume);
        return;
      }
    if(inputMode==SQUELCH)
      {
        squelch=squelch+1;
        if(squelch < 0) squelch=0;
        if(squelch > maxsql) squelch=maxsql;
        bandSquelch[band]=squelch;
        setSquelch(squelch);
        return;
      }
    if(inputMode==RIT)
      {
        rit=rit*10;
        if(rit < minrit) rit=minrit;
        if(rit > maxrit) rit=maxrit;
        setRit(rit);
        return;
      }
  }

// Volume Button


if(buttonTouched(volButtonX,volButtonY))    //Vol
    {
      if(inputMode==VOLUME)
        {
        setInputMode(FREQ);
        }
      else
        {
        setInputMode(VOLUME);
        }
      return;
    }



// Squelch Button


if(buttonTouched(sqlButtonX,sqlButtonY))    //sql
    {
     if((mode==FM) || (mode==DMR))
     {
      if(inputMode==SQUELCH)
        {
        setInputMode(FREQ);
        }
      else
        {
        setInputMode(SQUELCH);
        }
      return;
      }
    }


//RIT Button

if(buttonTouched(ritButtonX,ritButtonY))    //rit
    {
     if(mode!=4)
     {
      if(inputMode==RIT)
        {
        setInputMode(FREQ);
        }
      else
        {
        setInputMode(RIT);
        }
      return;
      }
    }

//RIT Zero Button

if(buttonTouched(ritButtonX,ritButtonY+buttonSpaceY))    //rit zero
    {
     setRit(0);
     setInputMode(FREQ);
    }

if(buttonTouched(sMeterX,sMeterY))                        //touch on s-Meter
{
   if(sMeterType == 0)
   {
     sMeterType=1;
   }
   else
   {
     sMeterType=0;
   }
}

//Function Buttons


if(buttonTouched(funcButtonsX,funcButtonsY))    //Button 1 = BAND or MENU
    {
    if((inputMode==FREQ) && (popupSel!=BAND))
    {
      writeConfig();
      displayPopupBand();
      return;
    }
    else
    {
      setInputMode(FREQ);
      clearPopUp();
      return;
    }


    }
if(buttonTouched(funcButtonsX+buttonSpaceX,funcButtonsY))    //Button 2 = MODE or Blank
    {
     if((inputMode==FREQ) && (popupSel!=MODE))
      {
      displayPopupMode();
      return;
      }
      else
      {
      setInputMode(FREQ);
      clearPopUp();
      return;
      }
    }

if(buttonTouched(funcButtonsX+buttonSpaceX*2,funcButtonsY))  // Button 3 =Blank or NEXT
    {
      if(inputMode==SETTINGS)
      {
      settingNo=settingNo+1;
      if(settingNo==numSettings) settingNo=0;
      displaySetting(settingNo);
      return;
      }
      else
      {
      setInputMode(FREQ);
      }
    }

if(buttonTouched(funcButtonsX+buttonSpaceX*3,funcButtonsY))    // Button4 =SET or PREV
    {
     if(inputMode==FREQ)
      {
      setInputMode(SETTINGS);
      return;
      }
    else  if (inputMode==SETTINGS)
      {
      settingNo=settingNo-1;
      if(settingNo<0) settingNo=numSettings-1;
      displaySetting(settingNo);
      return;
      }
    else
      {
      setInputMode(FREQ);
      return;
      }
    }

if(buttonTouched(funcButtonsX+buttonSpaceX*4,funcButtonsY))    //Button 5 =MONI (only allowed in Sat mode)  or Blank
    {
    if(inputMode==FREQ)
      {
      return;
      }
    else
      {
      setInputMode(FREQ);
      }


    }

if(buttonTouched(funcButtonsX+buttonSpaceX*5,funcButtonsY))    //Button 6 = Exit to Portsdown
    {
    if(inputMode==FREQ)
      {
      return;
      }
      else if (inputMode==SETTINGS)
      {
         setBandBits(0);
         sendFifo("H0");        //unlock the flowgraph so that it can exit
         sendFifo("Q");       //kill the SDR
         clearScreen();
         writeConfig();
         sleep(2);
         exit(0);             //return a value of zero to exit the run menu
      }
      else
      {
      setInputMode(FREQ);
      }
    }

if(buttonTouched(funcButtonsX+buttonSpaceX*6,funcButtonsY))   //Button 7 = OFF
    {
    if (inputMode==SETTINGS)
      {
      setBandBits(0);
      sendFifo("H");        //unlock the flowgraph so that it can exit
      sendFifo("Q");       //kill the SDR
      writeConfig();
      system("sudo cp /home/pi/Langstone/splash.bgra /dev/fb0");
      sleep(2);
      system("sudo poweroff");
      return;
      }
      else
      {
      setInputMode(FREQ);
      }


    }



//Touch on Frequency Digits moves cursor to digit and sets tuning step. Removes dial lock if it is set.

if((touchY>freqDisplayY) & (touchY < freqDisplayY+freqDisplayCharHeight) & (touchX>freqDisplayX) & (touchX < freqDisplayX+12*freqDisplayCharWidth))
  {
    if(inputMode==FREQ) setDialLock(0);
    int tx=touchX-freqDisplayX;
    tx=tx/freqDisplayCharWidth;
    tuneDigit=tx;
    setFreqInc();
    setFreq(freq);
    return;
  }

//touch on spectrum display increments the FFT Bandwidth
if((touchY < FFTY) & (touchY > FFTY - spectrum_rows) & (touchX > FFTX) & (touchX < FFTX + points))
  {
    bandFFTBW[band]++;
    if(bandFFTBW[band] > 3)  bandFFTBW[band] = 0;
    setFFTBW(bandFFTBW[band]);
    return;
  }


if(popupSel==MODE)
{
  for(int n=0;n<nummode;n++)
  {
     if(buttonTouched(popupX+(n*buttonSpaceX),popupY))
       {
       mode=n;
       setMode(mode);
       clearPopUp();
       }
  }
}

if(popupSel==BAND)
{
  if(buttonTouched(popupX,popupY))
  {
  popupFirstBand=popupFirstBand+6;
  if(bands24)
  {
    if(popupFirstBand>23) popupFirstBand=0;
  }
  else
  {
    if(popupFirstBand>11) popupFirstBand=0;
  }
  displayPopupBand();
  }

  for(int n=1;n<7;n++)
  {
     if(buttonTouched(popupX+(n*buttonSpaceX),popupY))
       {
       band=n-1+popupFirstBand;
       setBand(band);
       clearPopUp();
       }
  }


}
}


int buttonTouched(int bx,int by)
{
  return ((touchX > bx) & (touchX < bx+buttonWidth) & (touchY > by) & (touchY < by+buttonHeight));
}

void setBand(int b)
{
  freq=bandFreq[band];
  setFreq(freq);
  mode=bandMode[band];
  setMode(mode);
  setFFTBW(bandFFTBW[band]);
  setBandBits(bandBitsRx[band]);
  squelch=bandSquelch[band];
  setSquelch(squelch);
  FFTRef=bandFFTRef[band];
  configCounter=configDelay;
}

void setVolume(int vol)
{
  char volStr[10];
  sprintf(volStr,"V%d",vol);
  sendFifo(volStr);
  setForeColour(0,255,0);
  textSize=2;
  gotoXY(volButtonX+30,volButtonY-25);
  displayStr("   ");
  gotoXY(volButtonX+30,volButtonY-25);
  sprintf(volStr,"%d",vol);
  displayStr(volStr);

  configCounter=configDelay;
}

void setSquelch(int sql)
{
  char sqlStr[10];
  sprintf(sqlStr,"S%d",sql);
  sendFifo(sqlStr);
  if((mode==FM) || (mode==DMR))
  {
  setForeColour(0,255,0);
  textSize=2;
  gotoXY(sqlButtonX+30,sqlButtonY-25);
  displayStr("   ");
  gotoXY(sqlButtonX+30,sqlButtonY-25);
  sprintf(sqlStr,"%d",sql);
  displayStr(sqlStr);
  configCounter=configDelay;
  }
}

void setInputMode(int m)

{
if(inputMode==SETTINGS)
  {
  gotoXY(0,settingY);
  setForeColour(255,255,255);
  textSize=2;
  displayStr("                                                ");
  gotoXY(0,settingY+8);
  displayStr("                                                ");
  writeConfig();
  displayMenu();
  }
if(inputMode==VOLUME)
  {
    gotoXY(volButtonX,volButtonY);
    setForeColour(0,255,0);
    displayButton("Vol");
  }
if(inputMode==SQUELCH)
  {
    gotoXY(sqlButtonX,sqlButtonY);
    setForeColour(0,255,0);
    displayButton("SQL");
  }
if(inputMode==RIT)
  {
    gotoXY(ritButtonX,ritButtonY);
    setForeColour(0,255,0);
    displayButton("RIT");
    gotoXY(ritButtonX,ritButtonY+buttonSpaceY);
    setForeColour(0,0,0);
    displayButton("Zero");
  }

inputMode=m;

if(inputMode==FREQ)
  {
  setFreq(freq);
  }

if(inputMode==SETTINGS)
  {
    clearPopUp();
    gotoXY(funcButtonsX,funcButtonsY);
    setForeColour(0,255,0);
    displayButton("MENU");
    displayButton(" ");
    displayButton("NEXT");
    displayButton("PREV");
    displayButton(" ");
    setForeColour(255,0,0);
    if (portsdownPresent==1)
    {
        displayButton2x12("EXIT TO","PORTSDOWN");
    }
    else
    {
        displayButton1x12("EXIT");
    }
    displayButton1x12("SHUTDOWN");
    mouseScroll=0;
    displaySetting(settingNo);
  }
if(inputMode==VOLUME)
  {
    gotoXY(volButtonX,volButtonY);
    setForeColour(255,0,0);
    displayButton("Vol");
  }
if(inputMode==SQUELCH)
  {
    gotoXY(sqlButtonX,sqlButtonY);
    setForeColour(255,0,0);
    displayButton("SQL");
  }
if(inputMode==RIT)
  {
    gotoXY(ritButtonX,ritButtonY);
    setForeColour(255,0,0);
    displayButton("RIT");
    gotoXY(ritButtonX,ritButtonY+buttonSpaceY);
    setForeColour(255,0,0);
    displayButton("Zero");
  }

}

void setRit(int ri)
{
  char ritStr[10];
  int to;
  if(!((mode==FM) || (mode==AM) || (mode==DMR)))
  {
  rit=ri;
  setForeColour(0,255,0);
  textSize=2;
  to=28;
  gotoXY(ritButtonX,ritButtonY-25);
  displayStr("         ");
  gotoXY(ritButtonX+38-to,ritButtonY-25);
  if (rit==0)
  {
  sprintf(ritStr," 0.00");
  }
  else
  {
    sprintf(ritStr,"%+3.2f",rit/1000.0);
  }
  displayStr(ritStr);
  setFreq(freq);
  }
}

void setRxFilter(int low,int high)
{
  char filtStr[10];
  sprintf(filtStr,"I%d",low);
  sendFifo(filtStr);
  sprintf(filtStr,"F%d",high);
  sendFifo(filtStr);

  rxFilterLow = low;
  rxFilterHigh = high;

}

void setFFTBW(int bw)
{
  char BWStr[10];
  sprintf(BWStr,"W%d",bw);
  sendFifo(BWStr);

  switch(bw)
  {
  case 0:             //48KHz   Sample rate
  HzPerBin = 94;
  break;
  case 1:             //24KHz   Sample rate
  HzPerBin = 47;
  break;
  case 2:             //12KHz   Sample rate
  HzPerBin = 23;
  break;
  case 3:             //6KHz   Sample rate
  HzPerBin = 12;
  break;
  }
}



void setMode(int md)
{
  bandMode[band]=md;
  gotoXY(modeX,modeY);
  setForeColour(255,255,0);
  textSize=2;
  displayStr(modename[md]);
  displayStr("   ");

  if(md==USB)
    {
    sendFifo("M0");    //USB
    setRxFilter(bandSSBFiltLow[band],bandSSBFiltHigh[band]);    //USB Filter Setting    configured in settings.
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(0);
    ritButton(1);
    setRit(0);
    }

  if(md==LSB)
    {
    sendFifo("M1");    //LSB
    setRxFilter(-1*bandSSBFiltHigh[band],-1*bandSSBFiltLow[band]); // LSB Filter Setting
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(0);
    ritButton(1);
    setRit(0);
    }

  if(md==CW)
    {
    sendFifo("M2");    //CW
    setRxFilter(bandSSBFiltLow[band],bandSSBFiltHigh[band]);   // USB filter settings used for CW Wide Filter
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(0);
    ritButton(1);
    setRit(0);
    }

  if(md==CWN)
    {
    sendFifo("M3");    //CWN
    setRxFilter(600,1000);    //CW Narrow Filter
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(0);
    ritButton(1);
    setRit(0);
    }
  if(md==FM)
    {
    sendFifo("M4");    //FM
    setRxFilter(-7500,7500);    //FM Filter
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(1);
    ritButton(0);
    setRit(0);
    }
  if(md==AM)
    {
    sendFifo("M5");    //AM
    setRxFilter(-5000,5000);    //AM Filter
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(0);
    ritButton(0);
    setRit(0);
    }
  if (md==DMR)
    {
    sendFifo("M6");    // DSD DMR RX
    setRxFilter(-7500, 7500);    //FM Filter
    setFreq(freq);    //set the frequency to adjust for CW offset.
    sqlButton(1);
    ritButton(0);
    setRit(0);
    }


configCounter=configDelay;
}

void setHwRxFreq(double fr)
{
  long long rxoffsethz;
  long long LOrxfreqhz;
  long long rxfreqhz;
  double frRx;

  frRx=fr+bandRxOffset[band];

  if(bandRxHarmonic[band] <2)
  {
    if(frRx > maxHwFreq)
    {
    frRx = maxHwFreq;
    }
    if(frRx < minHwFreq)
    {
    frRx = minHwFreq;
    }
  }

  rxfreqhz=frRx*1000000;

  rxoffsethz=(rxfreqhz % 100000)+50000;        //use just the +50Khz to +150Khz positive side of the sampled spectrum. This avoids seeing the DC hump .
  LOrxfreqhz=rxfreqhz-rxoffsethz;

  if( bandRxHarmonic[band]>1)                                //allow for harmonic mixing for higher bands (10GHz)
    {
      LOrxfreqhz=LOrxfreqhz/bandRxHarmonic[band];
    }


  rxoffsethz=rxoffsethz+rit;
  if((mode==CW)|(mode==CWN))
    {
     rxoffsethz=rxoffsethz-800;         //offset  for CW tone of 800 Hz
    }
  if(LOrxfreqhz!=lastLOhz)
    {
      setRtlSdrRxFreq(LOrxfreqhz);
      lastLOhz=LOrxfreqhz;
    }

  char offsetStr[32];
  sprintf(offsetStr,"O%d",rxoffsethz);   //send the rx offset tuning value
  sendFifo(offsetStr);
}

void displayFreq(double fr)
{
  long long freqhz;
  char digit[16];

  fr=fr+0.0000001;   // correction for rounding errors.
  freqhz=fr*1000000;
  freqhz=freqhz+100000000000;     //force it to be 12 digits long
  sprintf(digit,"%lld",freqhz);

  gotoXY(freqDisplayX,freqDisplayY);
  setForeColour(0,0,255);
  textSize=6;
  if(digit[1]>'0') displayChar(digit[1]); else displayChar(' ');
  if((digit[1]>'0') | (digit[2]>'0')) displayChar(digit[2]); else displayChar(' ');
  if((digit[1]>'0') | (digit[2]>'0') | digit[3]>'0')  displayChar(digit[3]); else displayChar(' ');
  displayChar(digit[4]);
  displayChar(digit[5]);
  displayChar('.');
  displayChar(digit[6]);
  displayChar(digit[7]);
  displayChar(digit[8]);
  displayChar('.');
  displayChar(digit[9]);
  displayChar(digit[10]);

// Underline the currently selected tuning digit

  for(int dtd=0;dtd<12;dtd++)
    {
      gotoXY(freqDisplayX+dtd*freqDisplayCharWidth+4,freqDisplayY+freqDisplayCharHeight+5);
      int bb = 0;
      if (dtd==tuneDigit) bb=255;
      for(int p=0; p < freqDisplayCharWidth; p++)
        {
          setPixel(currentX+p,currentY+1,0,0,bb);
          setPixel(currentX+p,currentY+2,0,0,bb);
          setPixel(currentX+p,currentY+3,0,0,bb);
        }
    }

}

void setFreq(double fr)
{

  setHwRxFreq(fr);       //set Hardware Rx frequency if we are receiving
  displayFreq(fr);

   gotoXY(funcButtonsX+buttonSpaceX*4,funcButtonsY);
   setForeColour(0,255,0);
   if(inputMode==FREQ)
   {
     displayButton("    ");
     setForeColour(0,0,0);
     displayButton("    ");
     displayButton("    ");
   }

 configCounter=configDelay;                       //write config after this amount of inactivity
}

void setDialLock(int d)
{
  if(d==0)
  {
    dialLock=0;
    gotoXY(diallockX,diallockY);
    textSize=2;
    displayStr("    ");
  }
  else
  {
    dialLock=1;
    gotoXY(diallockX,diallockY);
    textSize=2;
    setForeColour(255,0,0);
    displayStr("LOCK");
  }
}

void setBandBits(int b)
{
if(hyperPixelPresent==0)                //dont use Raspberry Pi GPIO with Hyperpixel Display
  {
    if(b & 0x01)
        {
        digitalWrite(bandPin1,HIGH);
        digitalWrite(bandPin1alt,HIGH);
        }
    else
        {
        digitalWrite(bandPin1,LOW);
        digitalWrite(bandPin1alt,LOW);
        }

    if(b & 0x02)
        {
        digitalWrite(bandPin2,HIGH);
        }
    else
        {
        digitalWrite(bandPin2,LOW);
        }

    if(b & 0x04)
        {
        digitalWrite(bandPin3,HIGH);
        }
    else
        {
        digitalWrite(bandPin3,LOW);
        }

    if(b & 0x08)
        {
        digitalWrite(bandPin4,HIGH);
        }
    else
        {
        digitalWrite(bandPin4,LOW);
        }

    if(b & 0x10)
        {
        digitalWrite(bandPin5,HIGH);
        }
    else
        {
        digitalWrite(bandPin5,LOW);
        }

    if(b & 0x20)
        {
        digitalWrite(bandPin6,HIGH);
        }
    else
        {
        digitalWrite(bandPin6,LOW);
        }

    if(b & 0x40)
        {
        digitalWrite(bandPin7,HIGH);
        }
    else
        {
        digitalWrite(bandPin7,LOW);
        }

    if(b & 0x80)
        {
        digitalWrite(bandPin8,HIGH);
        }
    else
        {
        digitalWrite(bandPin8,LOW);
        }
  }



if(MCP23017Present==1)                       //optional extender chip has port b for band bits.
  {
  mcp23017_writeport(mcp23017_addr,GPIOB,b);
  }
}


void changeSetting(void)
{
   if(settingNo==RX_OFFSET)        //Transverter Rx Offset
      {
        bandRxOffset[band]=bandRxOffset[band]-mouseScroll*freqInc;
        if(bandRxOffset[band] > 99999.9) bandRxOffset[band]= 99999.9;
        if(bandRxOffset[band] < -99999.9) bandRxOffset[band]= -99999.9;

        freq=freq+mouseScroll*freqInc;

        if((freq + bandRxOffset[band]) > maxHwFreq )
        {
          freq = freq - ((freq + bandRxOffset[band]) - maxHwFreq);
        }

        if((freq + bandRxOffset[band]) < minHwFreq )
        {
          freq = freq + ( minHwFreq - (freq + bandRxOffset[band]));
        }
        mouseScroll=0;
        setFreq(freq);
        displaySetting(settingNo);
      }
 if(settingNo==RX_HARMONIC)        // RX Harmonic mixing number
      {
      if(mouseScroll>0)
      {
        bandRxHarmonic[band]=5;
      }
      if(mouseScroll<0)
      {
         bandRxHarmonic[band]=1;
      }
      mouseScroll=0;
      setFreq(freq);
      displaySetting(settingNo);
      }
   if(settingNo==BAND_BITS_RX)        // Band Bits Rx
      {
      if(setIndex==7)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x01;
        }
      if(setIndex==6)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x02;
        }
      if(setIndex==5)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x04;
        }
      if(setIndex==4)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x08;
        }
       if(setIndex==3)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x10;
        }
      if(setIndex==2)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x20;
        }
      if(setIndex==1)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x40;
        }
      if(setIndex==0)
        {
        bandBitsRx[band]=bandBitsRx[band] ^ 0x80;
        }
      mouseScroll=0;
      if(bandBitsRx[band]<0) bandBitsRx[band]=0;
      if(bandBitsRx[band]>255) bandBitsRx[band]=255;
      setBandBits(bandBitsRx[band]);
      displaySetting(settingNo);
      }

   if(settingNo==FFT_REF)        // FFT Ref Level
      {
      FFTRef=FFTRef+mouseScroll;
      mouseScroll=0;
      if(FFTRef<-80) FFTRef=-80;
      if(FFTRef>30) FFTRef=30;
      bandFFTRef[band]=FFTRef;
      displaySetting(settingNo);
      }
    if(settingNo==S_ZERO)        // S Meter Zero
      {
      bandSmeterZero[band]=bandSmeterZero[band]+mouseScroll;
      mouseScroll=0;
      if(bandSmeterZero[band]<-140) bandSmeterZero[band]=-140;
      if(bandSmeterZero[band]>-30) bandSmeterZero[band]=-30;
      displaySetting(settingNo);
      }
    if(settingNo==SSB_FILT_LOW)        // SSB Filter Low
      {
      bandSSBFiltLow[band]=bandSSBFiltLow[band]+mouseScroll*10;
      mouseScroll=0;
      if(bandSSBFiltLow[band] < 0) bandSSBFiltLow[band]=0;
      if(bandSSBFiltLow[band] >1000) bandSSBFiltLow[band]=1000;
      setMode(mode);                 //refresh mode to set new filter settings
      displaySetting(settingNo);
      }
    if(settingNo==SSB_FILT_HIGH)        // SSB Filter High
      {
      bandSSBFiltHigh[band]=bandSSBFiltHigh[band]+mouseScroll*10;
      mouseScroll=0;
      if(bandSSBFiltHigh[band] < 1000) bandSSBFiltHigh[band]=1000;
      if(bandSSBFiltHigh[band] > 5000) bandSSBFiltHigh[band]=5000;
      setMode(mode);                 //refresh mode to set new filter settings
      displaySetting(settingNo);
      }
    if(settingNo==BANDS24)        // 24 band mode
      {
      if(mouseScroll > 0)   bands24= 1;
      if(mouseScroll < 0)   bands24= 0;
      mouseScroll=0;
      displaySetting(settingNo);
      }
}

void displaySetting(int se)
{
  char valStr[30];
  gotoXY(0,settingY);
  textSize=2;
  setForeColour(255,255,255);
  displayStr("                                                ");
  gotoXY(0,settingY+8);
  displayStr("                                                ");
  gotoXY(settingX,settingY);
  if(se==RX_OFFSET)
  {
  gotoXY(0,settingY);
  }
  displayStr(settingText[se]);

  if(se==RX_OFFSET)
  {
  sprintf(valStr,"%.5f",bandRxOffset[band]);
  displayStr(valStr);
  displayStr(" Rx Freq= ");
  sprintf(valStr,"%.5f",freq+bandRxOffset[band]);
  displayStr(valStr);
  }
  if(se==RX_HARMONIC)
  {
  sprintf(valStr,"X%d",bandRxHarmonic[band]);
  displayStr(valStr);
  }
  if(se==BAND_BITS_RX)
  {
  maxSetIndex=7;
  setForeColour(255,255,255);
  for(int b=128;b>0;b=b>>1)
    {
      if(((setIndex==7)&&(b==1)) || ((setIndex==6)&&(b==2))  || ((setIndex==5)&&(b==4)) || ((setIndex==4)&&(b==8)) || ((setIndex==3)&&(b==16))  || ((setIndex==2)&&(b==32))  || ((setIndex==1)&&(b==64))  || ((setIndex==0)&&(b==128)))
        {
          setForeColour(0,255,0);
        }
      else
        {
          setForeColour(255,255,255);
        }
      if(bandBitsRx[band] & b)
        {
        displayChar('1');
        }
      else
        {
        displayChar('0');
        }
    }
  }

  if(se==FFT_REF)
  {
  sprintf(valStr,"%d",FFTRef);
  displayStr(valStr);
  }
  if(se==S_ZERO)
  {
  sprintf(valStr,"%.0f dB",bandSmeterZero[band]);
  displayStr(valStr);
  }
  if(se==SSB_FILT_LOW)
  {
  sprintf(valStr,"%d Hz",bandSSBFiltLow[band]);
  displayStr(valStr);
  }
  if(se==SSB_FILT_HIGH)
  {
  sprintf(valStr,"%d Hz",bandSSBFiltHigh[band]);
  displayStr(valStr);
  }
  if(se==BANDS24)
  {
    if(bands24 == 0)
    {
      displayStr("No");
    }
    else
    {
        displayStr("Yes");
    }
  }
}

int readConfig(void)
{
FILE * conffile;
char variable[50];
char value[100];
char vname[20];

conffile=fopen("/home/pi/Langstone/Langstone_RtlSdr.conf","r");

if(conffile==NULL)
  {
    return -1;
  }

while(fscanf(conffile,"%49s %99s [^\n]\n",variable,value) !=EOF)
  {

    for(int b=0;b<numband;b++)
    {
    sprintf(vname,"bandFreq%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%lf",&bandFreq[b]);
    sprintf(vname,"bandMode%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandMode[b]);
    sprintf(vname,"bandRxOffSet%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%lf",&bandRxOffset[b]);
    sprintf(vname,"bandRxHarmonic%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandRxHarmonic[b]);

 //handle old config file format by converting bandbits to bandbitsRx
    sprintf(vname,"bandBits%02d",b);
    if(strstr(variable,vname))
    {
    sscanf(value,"%d",&bandBitsRx[b]);
    }

    sprintf(vname,"bandRxBits%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandBitsRx[b]);
    sprintf(vname,"bandFFTRef%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandFFTRef[b]);
    sprintf(vname,"bandSquelch%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandSquelch[b]);
    sprintf(vname,"bandSmeterZero%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%f",&bandSmeterZero[b]);
    sprintf(vname,"bandSSBFiltLow%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandSSBFiltLow[b]);
    sprintf(vname,"bandSSBFiltHigh%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandSSBFiltHigh[b]);
    sprintf(vname,"bandFFTBW%02d",b);
    if(strstr(variable,vname)) sscanf(value,"%d",&bandFFTBW[b]);
    }

    if(strstr(variable,"currentBand")) sscanf(value,"%d",&band);
    if(strstr(variable,"tuneDigit")) sscanf(value,"%d",&tuneDigit);
    if(strstr(variable,"mode")) sscanf(value,"%d",&mode);
    if(strstr(variable,"volume")) sscanf(value,"%d",&volume);
    if(strstr(variable,"bands24")) sscanf(value,"%d",&bands24);
    if(mode>nummode-1) mode=0;

  }

fclose(conffile);
return 0;

}

int writeConfig(void)
{
FILE * conffile;
char variable[80];
int value;

bandFreq[band]=freq;

for(int i=0;i<MORSEIDENTLENGTH;i++)                                                 //trim morse Ident to remove redundant spaces.
  {
  if((morseIdent[i]==95) && (morseIdent[i+1]==95))                    //find double space
    {
      morseIdent[i]=0;                                                //terminate string here
      break;
    }
  }


conffile=fopen("/home/pi/Langstone/Langstone_RtlSdr.conf","w");

if(conffile==NULL)
  {
    return -1;
  }

for(int b=0;b<numband;b++)
{
  fprintf(conffile,"bandFreq%02d %lf\n",b,bandFreq[b]);
  fprintf(conffile,"bandMode%02d %d\n",b,bandMode[b]);
  fprintf(conffile,"bandRxOffSet%02d %lf\n",b,bandRxOffset[b]);
  fprintf(conffile,"bandRxHarmonic%02d %d\n",b,bandRxHarmonic[b]);
  fprintf(conffile,"bandRxBits%02d %d\n",b,bandBitsRx[b]);
  fprintf(conffile,"bandFFTRef%02d %d\n",b,bandFFTRef[b]);
  fprintf(conffile,"bandSquelch%02d %d\n",b,bandSquelch[b]);
  fprintf(conffile,"bandSmeterZero%02d %f\n",b,bandSmeterZero[b]);
  fprintf(conffile,"bandSSBFiltLow%02d %d\n",b,bandSSBFiltLow[b]);
  fprintf(conffile,"bandSSBFiltHigh%02d %d\n",b,bandSSBFiltHigh[b]);
  fprintf(conffile,"bandFFTBW%02d %d\n",b,bandFFTBW[b]);
}

fprintf(conffile,"currentBand %d\n",band);
fprintf(conffile,"tuneDigit %d\n",tuneDigit);
fprintf(conffile,"mode %d\n",mode);
fprintf(conffile,"volume %d\n",volume);
fprintf(conffile,"bands24 %d\n",bands24);

fclose(conffile);
return 0;

}
