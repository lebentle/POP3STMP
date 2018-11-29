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
static int noopResponse(int fd);
static int quitResponse(int fd,const char*);
static int sendAllMessageNumAndSize(int fd, int numberOfMessages,size_t totalSize,mail_list_t list);
static int sendTerminator(int fd);
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

// TODO: I am changing size_t into an int. I am not sure this is type
// safe 
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
	const char *list = "LIST";
	const char *quitMessage = "";
	int AuthortativeState = 0;
	int TransactionState = 0;
	// allocate this here
	mail_list_t mailList;

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
			return;
		}
		printf("print the current buffer\n");
		printf("%s",buffer);

		// check to see if space in buffer 
		char *token;
		const char s[2] = " ";
		// remove crlf TODO: THIS MIGHT CAUSE TROUBLE 
		// Sending out an email; use as a placeholder for now
		buffer[strcspn(buffer, "\r\n")] = '\0';
		token = strtok(buffer,s);

		// USER message
		if ((strcasecmp("USER",token) == 0) && AuthortativeState) {
			// get 2nd token which should be usernmame
			// and copy it a new user variable for pass authentication
			// if it is a valid user 
			// If invalid user; set user var to NULL  
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
		// PASS case 
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
				mailList = load_user_mail(user);
				int numMsgs = get_mail_count(mailList);
				TransactionState = 1;
				AuthortativeState = 0;
				int sizeMsgs = get_mail_list_size(mailList);
				if (send_string(fd, Resp, user,numMsgs,sizeMsgs) == -1){
					return;
				}
			}
		// STAT CASE
		} else if ((strcasecmp(stat, token) == 0) && TransactionState) {
				// since in transacationState we know the mailList has been
				// loaded in
				int numMsgs = get_mail_count(mailList);
				size_t sizeMsgs = get_mail_list_size(mailList);
				if (send_string(fd,"+Ok %d %zu",numMsgs,sizeMsgs) == -1){
					return;
				}
			}
		// LIST CASE 	
		 else if ((strcasecmp(list,token) == 0) && TransactionState){
		 	// since in transacationState we know the mailList has been
			// loaded in
			char *listArg = strtok(NULL, s);
			int numMsgs = get_mail_count(mailList);
			size_t sizeMsgs = get_mail_list_size(mailList); 
			// case no arg given with List
			if (!listArg) {
				if (!sendAllMessageNumAndSize(fd, numMsgs,sizeMsgs,mailList)){
					return;
				}
			}
			// Also, need to consider the delete case
			mail_item_t mailItem = get_mail_item(mailList,(unsigned int) listArg - '0');
			if (mailItem) {
				size_t msgSizeBytes = get_mail_item_size(mailItem);
				if (send_string(fd,"+Ok %d %zu \r\n",numMsgs,msgSizeBytes) == -1){
					return;
				}
			} else  {
				if (send_string(fd,"-ERR no such message, only %d messages in maildrop\r\n",numMsgs) == -1){
					return;
				}
			}
		} else if (strcasecmp(nope, token) == 0) {
			noopResponse(fd);
		} else if (!strcasecmp(quit, buffer)) {
			if (quitResponse(fd, user)) {
				printf("Closing the connection");
				break;
			}
		} else {
			if (send_string(fd, "%s", "-ERR command not recognized") == -1) {
				return;
			}
		}
		printf("now entering another iteration of a loop\n");
		memset(buffer, 0, sizeof(buffer));
	}
	printf("about to return\n");
	return;
}

// NOOP command functionality
int noopResponse(int fd){
	printf("I am entering is nope function\n");
	if (send_string(fd, "+OK\r\n") == -1) {
		return 0;
	}
	return 1;
}

int quitResponse(int fd, const char* user) {
	// use a generic message right 
	// TODO: cal size of msg + max user name size 
	if (send_string(fd, "+OK %s POP3 server signing off", user) == -1) {
		return 0;
	}
	return 1; 
}

int sendAllMessageNumAndSize(int fd, int numberOfMessages, size_t totalSize, mail_list_t list) {
	if (send_string(fd, "+OK %d messages (%zu octets)",numberOfMessages,totalSize) == -1) {
		return 0;
	}
	for (int i = 0; i < numberOfMessages; i++) {
		mail_item_t mailItem = get_mail_item(list,i);
		if (mailItem) {
			size_t msgSizeBytes = get_mail_item_size(mailItem);
			if (send_string(fd,"%d %zu",i, msgSizeBytes) == -1)
				return 0;
		}
	}
	if (!sendTerminator(fd)){
		return 0;
	}
	return 1;
}

int sendTerminator(int fd){
	if (send_string(fd, "%s\r\n", ".") == -1) {
		return 0;
	}
	return 1;
}

/*
// NO; just need to read in one message 
int readAllMessagesToClient(int fd, int numberOfMessages, mail_list_t list) {
	char buffer[MAX_LINE_LENGTH];
	for (int i = 0; i < numberOfMessages; i++) {
		// how_much_to_read_in 
		printf("reading in message");
		readInMessage = get_mail_item(list,i);

	}
}
*/ 

