/*
 * Camera IP H265 POE 3Mp ONVIF P2P
 *
 * Pilotage sub stream
 *
 * Utilisation:
 *
 * cam_ctl IP CODEC FPS RESOLUTION BITRATE
 *
 * Codec: h264 h265 FPS: 1 à 25 Resolution: 720 (720x480) 640 ( 640x360) 352 (352x288)
 *
 * exemple: cam_ctl 192.168.0.24 h265 20 640 180
 *                                            ^
 *                                         Bitrate
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

//int main(char CamIp[20], char Codec[6], int FPS, int Resolution, int Bitrate)
int main(int argc, char *argv[])
{
  char CamIp[20];
  char Codec[10];
  int FPS;
  int Resolution;
  int Bitrate;

  char Url[500];
  char Body[500];

  int Width;
  int Height;

  strcpy(CamIp, argv[1]);
  strcpy(Codec, argv[2]);
  FPS=atoi(argv[3]);
  Resolution=atoi(argv[4]);
  Bitrate=atoi(argv[5]);

  if (Resolution == 720)
  {
    Width=720;
    Height=480;
  }
  else if (Resolution == 640)
  {
    Width=640;
    Height=360;
  }
  else if (Resolution == 352)
  {
    Width=352;
    Height=288;
  }
  else
  {
    Width=352;
    Height=288;
  }


  fprintf(stderr, "IP: %s // Codec: %s // FPS: %d // Résolution: %d // Bitrate: %d\n", CamIp, Codec, FPS, Resolution, Bitrate);

  CURL *curl;
  CURLcode res;

  curl = curl_easy_init();
  snprintf(Url, 500, "http://%s/cgi-bin/jvsweb.cgi?cmd={\"method\":\"stream_set_params\",\"user\":{\"name\":\"admin\",\"digest\":\"479ead4555b49227dc8812d06970c652\"},\"param\":{\"streams\":[{\"channelid\":0,\"streamid\":1,\"venctype\":\"%s\",\"width\":%d,\"height\":%d,\"frameRate\":%d,\"bitRate\":%d,\"ngop_s\":100,\"quality\":80,\"rcMode\":\"cbr\",\"smartencode\":\"close\"}]}}", CamIp, Codec, Width, Height, FPS, Bitrate);
  curl_easy_setopt(curl, CURLOPT_URL, Url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");

  res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    return 1;
  }

  curl_easy_cleanup(curl);

  return 0;
}
