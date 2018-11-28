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
static int quitResponse(int fd,const char*);
// Things to consider further down the line
// what to send if send_string returns -1 
// Case where no user provided or pw: Can I just rid of the crlf? 
// What impact does this have on where we are reading in data for an email
// RIGHT NOW I JUST reset the life to ignore crlf. 
// The Case where:
// User logins -> invalid user
// Password provided -> user is still invalid
// What should the error message be?
// CASE:
// USER: OK 
// USER: Err
// PASS: Should this work??  
int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}

void print_hex(const char *s) {
if (s == NULL){
	printf("str is empty, return\n");
	return;
}
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
	const char *nope = "NOOP";
	const char *quit = "QUIT";
	const char *stat = "STAT";
	const char *quitMessage = "";
	int AuthortativeState = 0;
	int TransactionState = 0;
	char *tempUser;

	// todo get maximum user size
	char user[MAX_PASSWORD_SIZE];
	memset(user, '\0', sizeof(user));

	char *pw;
	// send initial welcome message
	if (send_string(fd,"%s",welecomeMessage) == -1) {
		printf("Hit an Error. TODO. Quit?");
	}
	// Successfully sent first message
	AuthortativeState = 1;
	char buffer[MAX_LINE_LENGTH];

	// now wait for a response 
	while (1) {
		if (nb_read_line(bufferReceievePointer,buffer) == -1) {
			printf("Hit an Error. TODO. Quit?");
			return;
		}
		printf("print the current buffer\n");
		printf("%s",buffer);

		// check to see if space in buffer 
		char *token;
		const char s[2] = " ";
		// remove crlf TODO: THIS MIGHT CAUSE TROUBLE 
		// READING IN EMAIL; use as a placeholder for now
		buffer[strcspn(buffer, "\r\n")] = '\0';
		token = strtok(buffer,s);
		if (token == NULL) {
			// need to send error 
			printf("Is NULL\n");
		}
		if ((strcasecmp("USER",token) == 0) && AuthortativeState) {
			printf("first part of message is USER\n");
			// get 2nd token which should be usernme
			// and save it 
			tempUser = strtok(NULL, s);
			char *str = "+OK %s is a valid mailbox";
			if (tempUser == NULL) {
				str = "-ERR Need Username passed as parameter%s";
			} else if (!is_valid_user(tempUser, NULL)) {
				str = "-ERR never heard of mailbox %s";
				strcpy(user,tempUser);
			} else {
				strcpy(user,tempUser);
			}
			printf("user is %s\n",tempUser);
			if (send_string(fd, str, tempUser) == -1){
				printf("Hit an Error. TODO. Quit?");
				return;
			}
		} else if ((strcasecmp("PASS",token) == 0) && AuthortativeState){
			const char *Resp = "+OK %s's maildrop has %d messages (%d octets)";
			pw = strtok(NULL, s);
			if (!strcasecmp(user,"")) {
				printf("USER is NULL");
				Resp = "-ERR No USER provided before PASS command.";
			} else if (!pw) {
				Resp = "-ERR Need password passed as a parameter";
				if (send_string(fd,"%s",Resp)){
					return;
				}
			} else if (!is_valid_user(user, pw)) {
				Resp = "-ERR incorrect password %s";
				// reset pw for next iteration
				if (send_string(fd,Resp,pw) == -1){
					return;
				}
				pw = NULL;
			} else {
				mail_list_t mailList = load_user_mail(user);
				int numMsgs = get_mail_count(mailList);
				TransactionState = 1;
				AuthortativeState = 0;
				int sizeMsgs = get_mail_list_size(mailList);
				if (send_string(fd, Resp, user,numMsgs,sizeMsgs) == -1){
					return;
				}
			}
		} else if (strcasecmp() &&TransactionState) 


		else if (strcasecmp(nope, buffer) == 0) {
			noopResponse(fd);
		} else if (!strcasecmp(quit, buffer)) {
			if (quitResponse(fd, user)) {
				printf("Closing the connection");
				break;
			}
		} else {
			if (send_string(fd, "%s", "-ERR command not recognized") == -1) {
				printf("Hit an sending a message Error. TODO. Quit?\n");
				return;
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
	if (send_string(fd, "+OK %s POP3 server signing off", user) == -1) {
		printf("Hit an Error. TODO. Quit?");
		return 0;
	}
	return 1; 
}

