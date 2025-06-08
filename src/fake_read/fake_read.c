#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
    int num;
    int ret;

    int VideoTS;

    char status_message[14];

    VideoTS = open("videots", O_RDONLY);
    if (VideoTS<0) printf("Failed to open videots fifo\n");

    printf("Listening\n");

    while (1) {
        num=read(VideoTS, status_message, 1);
        status_message[num]='\0';
        if (num>0) printf("%s",status_message);
    }
    close(VideoTS);
    return 0;
}
