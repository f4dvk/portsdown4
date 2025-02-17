#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>

int main(int argc, char ** argv) {
  int fd;
  int test;
  struct pollfd fds[1];
  char command[10];
  char port[10];
  char Port[15];

  if(argc != 3)
  {
    fprintf(stderr, "usage: %s port (ttyUSB0, ttyACM0, etc...) commande\n", argv[0]);
    return -1;
  }

  strncpy(command, argv[2], 10);
  strcat(command, "\n");
  command[9] = '\0';

  strncpy(port, argv[1], 10);
  port[9] = '\0';

  snprintf(Port, 15, "/dev/%s", port);
  fd = open(Port, O_RDWR | O_NOCTTY | O_NDELAY);
  if (fd == -1) {
    fprintf(stderr, "Impossible d'ouvrir le port %s - \n", Port);
    return(-1);
  }

  // Turn off blocking for reads, use (fd, F_SETFL, FNDELAY) if you want that
  fcntl(fd, F_SETFL, 0);

  // Write to the port
  int n = write(fd, command, sizeof(command));
  if (n < 0)
  {
    perror("Write failed - ");
    return -1;
  }

  fds[0].fd=fd;
  fds[0].events = POLLIN;

  test = poll(fds, 1, 100); // attendre 100ms la reponse

  if (!test) // si pas de reponse
  {
    printf("Mute\n");
    close(fd);
    return -1;
  }

  // Read up to 10 characters from the port if they are there
  char buf[10];
  n = read(fd, buf, sizeof(buf));
  if (n < 0)
  {
    perror("Read failed - ");
    return -1;
  } else if (n == 0) printf("No data on port\n");
  else
  {
    buf[n] = '\0';
    printf("%s", buf);
  }

  close(fd);
  return 0;
}
