#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
static void handle_client(int fd);
static void noopResponse(int fd);
static void openAndReadFile();

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}


void handle_client(int fd) {
}