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

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}


void handle_client(int fd) {
	// TODO
	struct utsname unameData;

	uname(&unameData);
	const char *noop = "NOOP";
	const char *quit = "QUIT";
	const char *helo = "HELO ";
	const char *mail = "MAIL ";
	const char *rcpt = "RCPT ";
	const char *data = "DATA";
	const char *from_header = " FROM:<";
	const char *to_header = " TO:<";
	const char *mail_ender = ">\r\n";
	char *greet;
	char *to;
	char *from;
	char command[6];
	char previousCommand[6];
	char holder[8];
	const char *noParams[4] = {"HELO", "MAIL", "RCPT"};
	const char *notImplemented[5] = {"EHLO", "RSET", "VRFY", "EXPN", "HELP"};

	user_list_t rcptList = create_user_list();

	int dataMode = 0;
	int file;
	FILE *f = NULL;
	char filename[] = "data-XXXXXX";

	const char *welcomeMessage = "Simple Mail Transfer Service Ready\r\n";
	const char *closeMessage = "Service closing transmission channel\r\n";

	net_buffer_t bufferReceievePointer = nb_create(fd, MAX_LINE_LENGTH);
	char buffer[MAX_LINE_LENGTH];
	// send initial welcome message
	if (send_string(fd,"220 %s %s", unameData.nodename, welcomeMessage) == -1) {
		printf("Hit an Error.");
	}
	wLoop: while(1) {
		int lineLength = nb_read_line(bufferReceievePointer,buffer);
		if (lineLength == -1) {
			printf("Hit an Error. TODO. Quit?");
			return;
		}
		if (strcspn(buffer, "\r\n") == MAX_LINE_LENGTH) {
			send_string(fd, "500 Line too long.\r\n");
			continue;
		}

		printf("%s",buffer);

		if (dataMode == 0) {
			// get the first 5 characters to compare
			memcpy(previousCommand, &command, 6);
			memcpy(command, &buffer, 6);
			if (command[4] == '\r' && command[5] == '\n') {
				command[4] = '\0';
			}

			for (int i = 0; i<5; i++) {
				if (strcasecmp(notImplemented[i],command) == 0) {
					send_string(fd, "502 Command not implemented\r\n");
					goto wLoop;
				}
				if (i < 3) {
					if (strcasecmp(noParams[i], command) == 0) {
						send_string(fd, "504 Command parameters not implemented\r\n");
						goto wLoop;
					}
				}
			}

			// handle quit command
			if (strcasecmp(quit, command) == 0) {
				send_string(fd, "221 %s %s", unameData.nodename, closeMessage);
				break;
			}

			// handle noop commmand
			if (strcasecmp(noop, command) == 0) {
				send_string(fd, "250 OK\r\n");
				continue;
			}

			if (command[4] == ' ') {
				command[5] = '\0';
			}

			// handle HELO command
			if (strcasecmp(helo, command) == 0) {
				// helo command, start of new mail to send
				// so delete any existing rcpts
				destroy_user_list(rcptList);
				rcptList = create_user_list();

				// get the parameter
				greet = strchr(buffer, ' ');
				if ((greet[1] != '\r') && (greet[2] != '\n')) {
					send_string(fd, "250-%s greets%s", unameData.nodename, greet);
					continue;
				} else {
					send_string(fd, "504 Command parameter not implemented\r\n");
					continue;
				}
			}

			// handle MAIL command
			if (strcasecmp(mail, command) == 0) {
				// previous command must be HELO or DATA
				if ((strcasecmp(helo, previousCommand) != 0) &&
					(strcasecmp(data, previousCommand) != 0)) {
					send_string(fd, "503 Bad sequence of commands\r\n");
					continue;
				}

				// ' FROM:<email>' needs to check if specification is OK
				from = strchr(buffer, ' ');
				memcpy(holder, from, 7);
				holder[7] = '\0';

				// from's header is OK
				if (strcasecmp(holder, from_header) == 0) {
					// from's ender is OK
					if (strcasecmp(strchr(from, '>'), mail_ender) == 0) {
						send_string(fd, "250 OK\r\n");
						continue;
					}
				}
				// Other cases, the FROM format is no good
				send_string(fd, "555 MAIL FROM parameters not recognized or not implemented\r\n");
				continue;
			}

			// handle RCPT command
			if (strcasecmp(rcpt, command) == 0) {
				// previous command must be MAIL or RCPT
				if ((strcasecmp(mail, previousCommand) != 0) &&
					(strcasecmp(rcpt, previousCommand) != 0)) {
					send_string(fd, "503 Bad sequence of commands\r\n");
					continue;
				}

				// ' TO:<email>' needs to check if specification is OK
				to = strchr(buffer, ' ');
				memcpy(holder, to, 5);
				holder[5] = '\0';

				// to's header is OK
				if (strcasecmp(holder, to_header) == 0) {
					// to's ender is OK
					if (strcasecmp(strchr(to, '>'), mail_ender) == 0) {
						// This gets the email without < >
						int emailLength = strcspn(to, ">") - strcspn(to, "<");
						char email[emailLength];
						memcpy(email, &strchr(to, '<')[1], emailLength - 1);
						email[emailLength - 1] = '\0';

						// check if to's email is in the list
						if (is_valid_user(email, NULL)) {
							// store the recipient's email in the list
							add_user_to_list(&rcptList, email);

							send_string(fd, "250 OK\r\n");
							continue;
						}
						send_string(fd, "550 No such user here\r\n");
						continue;
					}
				}
				// Other cases, the TO format is no good
				send_string(fd, "555 RCPT TO parameters not recognized or not implemented\r\n");
				continue;
			}

			// handle DATA command
			if (strcasecmp(data, command) == 0) {
				// previous command must be RCPT
				if (strcasecmp(rcpt, previousCommand) != 0) {
					send_string(fd, "503 Bad sequence of commands\r\n");
					continue;
				}
				dataMode = 1;
				send_string(fd, "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
				if ((file = mkstemp(filename)) == -1) {
					printf("Failed to make file\r\n");
				}
				f = fdopen(file, "w");
				continue;
			}

			// Random input received, reject with 500
			send_string(fd, "500 Command not recognized\r\n");
		}
		if (dataMode) {
			if (strcasecmp(".\r\n", buffer) == 0) {
				dataMode = 0;
				// save the mail to each rcpt
				save_user_mail(filename, rcptList);

				// destroy temp file
				fclose(f);
				unlink(filename);
				send_string(fd, "250 OK\r\n");
				continue;
			}
			// save the data in a temp file
			fprintf(f, "%s", buffer);
		}
	}
}