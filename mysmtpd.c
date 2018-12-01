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
static int handle_helo(int fd, char *buffer, user_list_t rcptList, struct utsname unameData);
static int handle_mail(int fd, char *previousCommand, char *buffer, char *holder);
static int handle_data(int fd, char *previousCommand);
static int write_data(int fd, char *buffer, char *filename, FILE* f, user_list_t rcptList);

const char *noop = "NOOP";
const char *quit = "QUIT";
const char *helo = "HELO ";
const char *mail = "MAIL ";
const char *rcpt = "RCPT ";
const char *data = "DATA";
const char *mail_ender = ">\r\n";

int main(int argc, char *argv[]) {
  
  if (argc != 2) {
    fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
    return 1;
  }
  
  run_server(argv[1], handle_client);
  
  return 0;
}


void handle_client(int fd) {
	struct utsname unameData;
	uname(&unameData);

	char command[6];

	// this is a bit that tracks whether the last command was valid
	int validCommand = 0;
	// holds the last valid command
	char previousCommand[6];
	char holder[8];

	const char *noParams[4] = {"HELO", "MAIL", "RCPT"};
	const char *notImplemented[5] = {"EHLO", "RSET", "VRFY", "EXPN", "HELP"};
	const char *to_header = " TO:<";
	char *to;
	user_list_t rcptList = create_user_list();

	// this is a bit that tracks if data is being written
	// 0-not writing, 1-data being written
	int dataMode = 0;
	int file;
	FILE *f = NULL;
	char filename[] = "data-XXXXXX";

	net_buffer_t bufferReceievePointer = nb_create(fd, MAX_LINE_LENGTH);
	char buffer[MAX_LINE_LENGTH];
	// send initial welcome message
	if (send_string(fd,"220 %s\r\n", unameData.nodename) == -1) {
		return;
	}
	wLoop: while(1) {
		// read client input
		int lineLength = nb_read_line(bufferReceievePointer,buffer);
		if (lineLength == -1) {
			return;
		}
		if (strcspn(buffer, "\r\n") == MAX_LINE_LENGTH) {
			validCommand = 0;
			if (send_string(fd, "500 Line too long.\r\n") == -1) {
				return;
			}
			continue;
		}

		if (dataMode == 0) {
			// save the previous command if it was valid
			if (validCommand) {
				memcpy(previousCommand, &command, 6);
			}
			// get the first 5 characters to compare
			memcpy(command, &buffer, 6);
			if (command[4] == '\r' && command[5] == '\n') {
				command[4] = '\0';
			}

			for (int i = 0; i<5; i++) {
				if (strcasecmp(notImplemented[i],command) == 0) {
					validCommand = 0;
					if (send_string(fd, "502 Command not implemented\r\n") == -1) {
						return;
					}
					goto wLoop;
				}
				if (i < 3) {
					if (strcasecmp(noParams[i], command) == 0) {
						validCommand = 0;
						if (send_string(fd, "504 Command parameters not implemented\r\n") == -1) {
							return;
						}
						goto wLoop;
					}
				}
			}

			// handle quit command
			if (strcasecmp(quit, command) == 0) {
				// Set validCommand to 1 so that when opening a new connection
				// the previousCommand is overwritten
				validCommand = 1;
				if (send_string(fd, "221 %s Service closing transmission channel\r\n", unameData.nodename) == -1) {
					return;
				}
				break;
			}

			// handle noop commmand
			if (strcasecmp(noop, command) == 0) {
				// since noops do not interfere with the chain of commands
				// we will treat it as an invalid command
				validCommand = 0;
				if (send_string(fd, "250 OK\r\n") == -1) {
					return;
				}
				continue;
			}

			if (command[4] == ' ') {
				command[5] = '\0';
			}

			// handle HELO command
			if (strcasecmp(helo, command) == 0) {
				validCommand = handle_helo(fd, buffer, rcptList, unameData);
				if (validCommand == 2) {
					validCommand = 0;
					return;
				}
				continue;
			}

			// handle MAIL command
			if (strcasecmp(mail, command) == 0) {
				validCommand = handle_mail(fd, previousCommand, buffer, holder);
				if (validCommand == 2) {
					validCommand = 0;
					return;
				}
				continue;
			}

			// handle RCPT command
			if (strcasecmp(rcpt, command) == 0) {
				// previous command must be MAIL or RCPT
				if ((strcasecmp(mail, previousCommand) != 0) &&
					(strcasecmp(rcpt, previousCommand) != 0)) {
					if (send_string(fd, "503 Bad sequence of commands\r\n") == -1) {
						return;
					}
					validCommand = 0;
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
							if (send_string(fd, "250 OK\r\n") == -1) {
								return;
							}
							validCommand = 1;
							continue;
						}
					}
				}
				// Other cases, the TO format is no good
				if (send_string(fd, "555 RCPT TO parameters not recognized or not implemented\r\n") == -1) {
					return;
				}
				validCommand = 0;
				return;
			}

			// handle DATA command
			if (strcasecmp(data, command) == 0) {
				dataMode = handle_data(fd, previousCommand);
				if (dataMode == 2) {
					dataMode = 0;
					validCommand = 0;
					return;
				}
				validCommand = dataMode;
				if ((file = mkstemp(filename)) == -1) {
					printf("Failed to make file\r\n");
					if (send_string(fd, "451  Requested action aborted: error in processing\r\n") == -1) {
						return;
					}
				}
				f = fdopen(file, "w");
				continue;
			}
			validCommand = 0;
			// Random input received, reject with 500
			if (send_string(fd, "500 Command not recognized\r\n") == -1) {
				return;
			}
		}
		// write data
		if (dataMode) {
			dataMode = write_data(fd, buffer, filename, f, rcptList);
			if (dataMode == 2) {
				dataMode = 0;
				return;
			}
		}
	}
}

// handles HELO command, returns int validCommand
// 0: not valid
// 1: valid
// 2: some error, needs to exit out.
int handle_helo(int fd, char *buffer, user_list_t rcptList, struct utsname unameData) {
	char *greet;
	// get the parameter
	greet = strchr(buffer, ' ');
	if ((greet[1] != '\r') && (greet[2] != '\n')) {
		if (send_string(fd, "250-%s greets%s", unameData.nodename, greet) == -1) {
			return 2;
		}
		// successful helo command, start of new mail to send
		// so delete any existing rcpts
		destroy_user_list(rcptList);
		rcptList = create_user_list();
		return 1;
	} else {
		// when there is no parameter i.e. 'helo '
		if (send_string(fd, "504 Command parameter not implemented\r\n") == -1) {
			return 2;
		}
		return 0;
	}
}

// handles MAIL command, returns validCommand
int handle_mail(int fd, char *previousCommand, char *buffer, char *holder) {
	char *from;
	const char *from_header = " FROM:<";
	// previous command must be HELO or DATA
	if ((strcasecmp(helo, previousCommand) != 0) &&
		(strcasecmp(data, previousCommand) != 0)) {
		if (send_string(fd, "503 Bad sequence of commands\r\n") == -1) {
			return 2;
		}
		return 0;
	}

	// ' FROM:<email>' needs to check if specification is OK
	from = strchr(buffer, ' ');
	memcpy(holder, from, 7);
	holder[7] = '\0';

	// from's header is OK
	if (strcasecmp(holder, from_header) == 0) {
		// from's ender is OK
		if (strcasecmp(strchr(from, '>'), mail_ender) == 0) {
			if (send_string(fd, "250 OK\r\n") == -1) {
				return 2;
			}
			return 1;
		}
	}
	// Other cases, the FROM format is no good
	if (send_string(fd, "555 MAIL FROM parameters not recognized or not implemented\r\n") == -1) {
		return 2;
	}
	return 0;
}

// handles DATA command, returns validCommand/dataMode
// 0: not valid/not going to data
// 1: valid -> data mode
// 2: some error so abort
int handle_data(int fd, char *previousCommand) {
	// previous command must be RCPT
	if (strcasecmp(rcpt, previousCommand) != 0) {
		if (send_string(fd, "503 Bad sequence of commands\r\n") == -1) {
			return 2;
		}
		return 0;
	}
	if (send_string(fd, "354 Start mail input; end with <CRLF>.<CRLF>\r\n") == -1) {
		return 2;
	}
	return 1;
}

// Writes the data, returns dataMode
// if 0: end of data
// 1: still saving data
// 2: error when sending string
int write_data(int fd, char *buffer, char *filename, FILE* f, user_list_t rcptList) {
	if (strcasecmp(".\r\n", buffer) == 0) {
		// save the mail to each rcpt
		save_user_mail(filename, rcptList);

		// destroy temp file
		fclose(f);
		unlink(filename);
		if (send_string(fd, "250 OK\r\n") == -1) {
			return 2;
		}
		return 0;
	}
	// save the data in a temp file
	fprintf(f, "%s", buffer);
	return 1;
}