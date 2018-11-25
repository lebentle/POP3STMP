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

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void print_hex(const char *s)
{
  while(*s) {
    printf("%02x", (unsigned int) *s++);
}
  return;
}

void handle_client(int fd) {
	// creates a buffer for receiving a message
	// net_buffer_t bufferSendPointer = nb_create(fd,512);
	net_buffer_t bufferReceievePointer = nb_create(fd,512);
	// lets send hello message to the client 

	const char *welecomeMessage = "+OK POP3 server ready";
	const char *nope = "NOOP\n";
	const char *quit = "QUIT\n";
	const char *quitMessage = "";
	const char *user;
	const char *pw;
	// send initial welcome message
	if (send_string(fd,welecomeMessage) == -1) {
		printf("Hit an Error. TODO. Quit?");
	}
	char buffer[MAX_LINE_LENGTH];

	// now wait for a response 
	while (1) {
		printf("print the current buffer");
		if (nb_read_line(bufferReceievePointer,buffer) == -1) {
			printf("Hit an Error. TODO. Quit?");
			break;
		}
		printf("%s",buffer);
		// check to see if space in buffer 
		printf("result from the strcomp %d", strchr(buffer, ' '));
		if (strchr(buffer, ' ') != -1) {
			printf("message contains a space\n");
			char *token;
			// get the first token
			token = strtok(buffer, ' ');
			printf("got first part of message %s\n",token);
			if (strcasecmp("USER",token)) {
				printf("first part of message is USER\n");
				// get 2nd token which should be usernme
				// and save it 
				user = strtok(NULL, " ");
				printf("got second part of message %s",user);
				const char *str = "+OK %s is a valid mailbox";
				if (strcasecmp(user, "")) {
					str = "-ERR Need User Name passed as parameter";
				} else if (is_valid_user(user, NULL)) {
					printf("Not a valid user\n");
					str = "-ERR never heard of mailbox %s";
				}
				if (send_string(fd, str, user) == -1){
					printf("Hit an Error. TODO. Quit?");
				}
			}
		} 
		if (!strcasecmp(nope, buffer)) {
			printf("is a NOOP message\n");
			noopResponse(fd);
		} else if (!strcasecmp(quit, buffer)) {
			if (quitResponse(fd, user)) {
				printf("Closing the connection");
				break;
			}
		}
		printf("now entering another iteration of a loop\n");
		memset(buffer, 0, sizeof(buffer));
	}
	printf("about to return\n");
	return;
  // TODO To be implemented
}

// NOOP command functionality
void noopResponse(int fd){
	printf("I am entering is nope function\n");
	if (send_string(fd, "+OK\r\n") == -1) {
		printf("Hit an Error. TODO. Quit?\n");
	}
	printf("Sent Reponse\n");
}

int quitResponse(int fd, const char* user) {
	// use a generic message right 
	// TODO: cal size of msg + max user name size 
	char bufferExitMessage[1024];
	printf("entering quit function\n");
	int chars = sprintf(bufferExitMessage,"+OK %s POP3 server signing off", user);
	if (chars > 1024) {
		printf("****Memory Error****\n");
	}
	if (send_string(fd, bufferExitMessage) == -1) {
		printf("Hit an Error. TODO. Quit?");
		return 0;
	}
	return 1; 
}
