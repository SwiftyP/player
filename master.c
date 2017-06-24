#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "libssh2_config.h"
#include <libssh2.h>

#include <pthread.h>

#include <errno.h>
#include "err.h"


#define BUFFER_SIZE 1024

#define MAX_SIZE 1000

#define TRUE 1
#define FALSE 0

#define CONST 10
#define CONST_TIME 60

static const char start_string[] = "START";
static const char at_string[] = "AT";
static const char pause_string[] = "PAUSE";
static const char play_string[] = "PLAY";
static const char title_string[] = "TITLE";
static const char quit_string[] = "QUIT";


char *username = "";
char *password = "";

char *exec_line = "bash -l -c";
//char *path_line = "./Pulpit/zadanie2/player";
char *path_line = "./player";

char *rsa_pub = "/home/students/inf/m/mw305964/.ssh/id_rsa.pub";
char *rsa = "/home/students/inf/m/mw305964/.ssh/id_rsa";


int finish = FALSE;
int socket_fd = 0;
fd_set main_fd_set;
int players = 0;
char players_host_dictionary[MAX_SIZE][BUFFER_SIZE];
char players_port_dictionary[MAX_SIZE][BUFFER_SIZE];


struct arg_struct {
	char* hostname;
	char* player_parameter;
	int delay;
	int duration;
	int players;
};


int equals(const char* left_word, const char* right_word)
{
	if (left_word == NULL || right_word == NULL) {
		return FALSE;
	}
	if (strcmp(left_word, right_word) == 0) {
		return TRUE;
	}
	return FALSE;
}

void save_port_number(long *port, char *arg)
{
	char* rest;

	*port = strtol(arg, &rest, 10);
	if ((rest == arg) || (*rest != '\0')) {
		fatal("'%s' is not valid port number\n", arg);
	}
	if (*port < 0) {
		fatal("'%s' is not valid port number\n", arg);
	}
	if (*port > 65535) {
		fatal("'%s' is not valid port number\n", arg);
	}
}

int check_and_save_variable(long *variable, char *arg)
{
	char* rest;

	*variable = strtol(arg, &rest, 10);
	if ((rest == arg) || (*rest != '\0')) {
		bad("'%s' is not valid number\n", arg);
		return -1;
	}
	if (*variable < 0) {
		bad("'%s' is not valid positive number\n", arg);
		return -1;
	}
	return 0;
}

void run_server(struct sockaddr_in *server, long port) 
{
	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syserr("socket");
	}

	server->sin_family = AF_INET;
	server->sin_addr.s_addr = htonl(INADDR_ANY);
	server->sin_port = htons(port);
	memset(server->sin_zero, '\0', sizeof server->sin_zero);

	if (bind(socket_fd, (struct sockaddr *)server, sizeof(struct sockaddr)) < 0) {
		if (errno == EADDRINUSE) {
			bad("Port is not available, already to other process\n");
		} else {
			syserr("bind");
		}
	}

	if (listen(socket_fd, 10) < 0) {
		syserr("listen");
	}
}

static int wait_socket(int sock, LIBSSH2_SESSION *session)
{
	struct timeval timeout;
	int rc;
	fd_set fd;
	fd_set *writefd = NULL;
	fd_set *readfd = NULL;
	int dir;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	FD_ZERO(&fd);
	FD_SET(sock, &fd);

	/* now make sure we wait in the correct direction */
	dir = libssh2_session_block_directions(session);

	if (dir & LIBSSH2_SESSION_BLOCK_INBOUND) {
		readfd = &fd;
	}

	if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
		writefd = &fd;
	}

	rc = select(sock + 1, readfd, writefd, NULL, &timeout);
	if (rc < 0) {
		syserr("select");
	}

	return rc;
}

void write_normal_success_message(int client_id, long id)
{
	char client_message[BUFFER_SIZE];

	sprintf(client_message, "OK %ld\n", id);
	if (write(client_id , client_message , strlen(client_message)) < 0) {
		syserr("write");
	}
}

void write_title_success_message(int client_id, long id, char* title)
{
	char client_message[BUFFER_SIZE];

	sprintf(client_message, "OK %ld\n %s", id, title);
	if (write(client_id , client_message , strlen(client_message)) < 0) {
		syserr("write");
	}
}

int exec_command(char *hostname, char *player_parameter)
{

	int rc;
	char command_line[BUFFER_SIZE];
	unsigned long hostaddr;
	int sock;
	struct sockaddr_in sin;
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;

	memset(command_line, 0, sizeof(command_line));
	sprintf(command_line, "%s '%s %s'", exec_line, path_line, player_parameter);

	rc = libssh2_init(0);
	if (rc < 0) {
		syserr("libssh2_init");
	}

	hostaddr = inet_addr(hostname);
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		syserr("socket");
	}

	sin.sin_family = AF_INET;
	sin.sin_port = htons(22);
	sin.sin_addr.s_addr = hostaddr;
	if (connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) < 0) {
		syserr("connect");
	}

	session = libssh2_session_init();
	if (!session) {
		syserr("libssh2_session_init");
		return -1;
	}

	libssh2_session_set_blocking(session, 0);

	while ((rc = libssh2_session_handshake(session, sock)) == LIBSSH2_ERROR_EAGAIN);
	if (rc) {
		syserr("libssh2_session_handshake");
		return -1;
	}

	if (strlen(password) != 0) {
		while ((rc = libssh2_userauth_password(session, 
			username, password)) == LIBSSH2_ERROR_EAGAIN);
		if (rc) {
			syserr("libssh2_userauth_password");
			return -1;
		}
	} else {
		while ((rc = libssh2_userauth_publickey_fromfile(session, 
			username, rsa_pub, rsa, password)) == LIBSSH2_ERROR_EAGAIN);

		if (rc) {
			syserr("libssh2_userauth_publickey_fromfile");
			return -1;
		}
	}

	while ((channel = libssh2_channel_open_session(session)) == NULL && 
		libssh2_session_last_error(session,NULL,NULL,0) == LIBSSH2_ERROR_EAGAIN) {
		wait_socket(sock, session);
	}

	if (channel == NULL) {
		syserr("libssh2_channel_open_session");
		return -1;
	}

	while ((rc = libssh2_channel_exec(channel, command_line)) == LIBSSH2_ERROR_EAGAIN) {
		wait_socket(sock, session);
	}

	if (rc < 0) {
		syserr("libssh2_channel_exec");
		return -1;
	}

	if (close(sock) < 0) {
		syserr("close");
		return -1;
	}

	return 0;
}

int connect_server(long id)
{
	int sock, port;
	struct sockaddr_in server;
	struct hostent *host;
	char *hostname;

	hostname = players_host_dictionary[id];
	port = atoi(players_port_dictionary[id]);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		syserr("socket");
	}

	host = gethostbyname(hostname);
	if (host == NULL) {
		bad("No such host as %s\n", hostname);
		return -1;
	}

	bzero((char *) &server, sizeof(server));
	server.sin_family = AF_INET;
	bcopy((char *)host->h_addr, (char *)&server.sin_addr.s_addr, host->h_length);
	server.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1) {
		syserr("connect");
	}

	return sock;
}

int send_message(long id, char * message)
{
	int sock;
	char buffer[BUFFER_SIZE];

	sock = connect_server(id);
	if (sock < 0) {
		return -1;
	}

	memset(buffer, 0, sizeof(buffer));
	sprintf(buffer, "%s", message);
	if (send(sock, buffer, strlen(buffer), 0) < 0) {
		syserr("send");
	}

	if (close(sock) < 0) {
		syserr("close");
	}

	return 0;
}

int count_delay(char* time_string)
{
	char* hour_string;
	char* minute_string;
	int hour_int_now, minute_int_now, second_int_now;
	int hour_int_future, minute_int_future;
	time_t t;
	struct tm * timeinfo; 
	int result = 0;

	hour_string = strtok(time_string, ".");
	minute_string = strtok(NULL, ".");

	hour_int_future = atoi(hour_string);
	minute_int_future = atoi(minute_string);

	time (&t);
	timeinfo = localtime (&t);

	hour_int_now = timeinfo->tm_hour;
	minute_int_now = timeinfo->tm_min;
	second_int_now = timeinfo->tm_sec;

	if (hour_int_future < hour_int_now) {
		return -1;
	} else if (hour_int_future == hour_int_now) {
		if (minute_int_future <= minute_int_now) {
			return -1;
		} else {
			result = CONST_TIME - second_int_now;
			result += (minute_int_future - minute_int_now - 1)*CONST_TIME;
			return result;
		}
	} else {
		result = CONST_TIME - second_int_now;
		result += (CONST_TIME - minute_int_now - 1)*CONST_TIME;
		result += (hour_int_future - hour_int_now - 1)*CONST_TIME*CONST_TIME;
		result += minute_int_future*CONST_TIME;
		return result;
	}
}

void *exec_command_with_delay(void *arg)
{
	struct arg_struct *args = arg;

	sleep(args->delay);
	if (exec_command(args->hostname, args->player_parameter) < 0) {
		bad("exec_command");
	} else {
		sleep(args->duration*60);
		if (send_message(args->players, (char *)quit_string) < 0) {
			bad("send_message");
		}
	}
	pthread_exit(NULL);
}

int read_message(char *buffer, int client_id)
{
	long id;
	int i = 0;
	char *array[CONST];

	buffer[strcspn(buffer, "\r\n")] = 0; // remove LF, CR, CRLF, LFCR, ...
	array[i] = strtok(buffer, " ");
	while (array[i] != NULL) {
		array[++i] = strtok(NULL, " ");
	}

	if (equals(array[0], start_string)) {
		char player_parameter[BUFFER_SIZE];

		memset(players_host_dictionary[players], 0, sizeof(players_host_dictionary[players]));
		sprintf(players_host_dictionary[players], "%s", array[1]);
		
		memset(players_port_dictionary[players], 0, sizeof(players_port_dictionary[players]));
		sprintf(players_port_dictionary[players], "%s", array[6]);

		memset(player_parameter, 0, sizeof(player_parameter));
		sprintf(player_parameter, "%s %s %s %s %s %s", 
			array[2], array[3], array[4], array[5], array[6], array[7]);

		if (exec_command(array[1], player_parameter) < 0) {
			bad("couldn't exec command");
			return -1;
		} else {
			write_normal_success_message(client_id, players);
			players += 1;
			return 0;
		}

	} else if (equals(array[0], play_string) 
			|| equals(array[0], pause_string) 
			|| equals(array[0], quit_string)
			|| equals(array[0], title_string)) { 

		if (array[1] == NULL) {
			bad("id numer not given");
			return -1;
		} else {
			if (check_and_save_variable(&id, array[1]) < 0) {
				bad("%s is wrong id number", array[1]);
				return -1;
			} else {
				if (id < players) {
					if (send_message(id, array[0]) < 0) {
						bad("send_message");
						return -1;
					} else {
						write_normal_success_message(client_id, id);
						return 0;
					}
				} else {
					bad("Id %s is out of range", array[1]);
					return -1;
				}
			}
		}
	/* powinno byc osobno, ale nie dzialalo wczytywanie title
	} else if (equals(array[0], title_string)) {
		//<polecenie-playera> <id>
		//return OK <id> TITLE
		return 0;
	*/
	} else if (equals(array[0], at_string)) {
		char player_parameter[BUFFER_SIZE];

		memset(players_host_dictionary[players], 0, sizeof(players_host_dictionary[players]));
		sprintf(players_host_dictionary[players], "%s", array[3]);

		memset(players_port_dictionary[players], 0, sizeof(players_port_dictionary[players]));
		sprintf(players_port_dictionary[players], "%s", array[8]);

		memset(player_parameter, 0, sizeof(player_parameter));
		sprintf(player_parameter, "%s %s %s %s %s %s", 
			array[4], array[5], array[6], array[7], array[8], array[9]);

		int delay;
		delay = count_delay(array[1]);

		if (delay < 0) {
			bad("Given time is in the past");
			return -1;
		} else {
			pthread_t pt;
			struct arg_struct args;

			args.delay = delay;
			args.duration = atoi(array[2]);
			args.hostname = array[3];
			args.player_parameter = player_parameter;
			args.players = players;

			if (pthread_create(&pt, NULL, &exec_command_with_delay, (void *)&args) != 0) {
				bad("pthread_create");
				return -1;
			} else {
				write_normal_success_message(client_id, players);
				players += 1;
				return 0;
			}
		}
	} else {
		bad("%s is not valid option", buffer);
		return -1;
	}
}

int read_from_client (int client_id)
{
	char buffer[BUFFER_SIZE];
	int read_len;

	memset(buffer, 0, sizeof(buffer));
	read_len = read(client_id, buffer, BUFFER_SIZE);
	if (read_len < 0) {
		syserr("read");
		exit(EXIT_FAILURE); //bo non-void function
	} else if (read_len == 0) {
		return -1;
	} else {
		fprintf(stderr, "Got message: %s", buffer);
		return read_message(buffer, client_id);
	}
}

void handle_new_client(struct sockaddr_in *client)
{
	socklen_t addr_len;
	int new_socket_fd;

	addr_len = sizeof(struct sockaddr_in);
	new_socket_fd = accept(socket_fd, (struct sockaddr *)client, &addr_len);
	if (new_socket_fd < 0) {
		syserr("accept");
	}

	FD_SET(new_socket_fd, &main_fd_set);
}

int main(int argc, char *argv[])
{
	long port;
	int i;
	struct sockaddr_in server, client;
	fd_set read_fd_set;

	if (argc > 2) {
		fatal("Usage: %s [port]\n", argv[0]);
	}

	if (argc == 2) {
		/* Get given port number. */
		save_port_number(&port, argv[1]);
	} else {
		/* Just bind() socket setting sin_port to 0 in sockaddr_in,
		   system will automatically select unused port. */
		port = 0;
	}

	run_server(&server, port);
	if (argc == 1) {
		socklen_t len = sizeof(server);
		if (getsockname(socket_fd, (struct sockaddr *)&server, &len) < 0) {
			syserr("getsockname");
		}
		printf("%d\n", ntohs(server.sin_port));
	}

	FD_ZERO(&main_fd_set);
	FD_ZERO(&read_fd_set);

	FD_SET(socket_fd, &main_fd_set);

	while (1) {	
		read_fd_set = main_fd_set;
		if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
			syserr("select");
		}

		for (i = 0; i < FD_SETSIZE; ++i) {
			if (FD_ISSET(i, &read_fd_set)) {
				if (i == socket_fd) {
					handle_new_client(&client);
				} else {
					if (read_from_client(i) < 0) {
						if (close(i) < 0) {
							syserr("close");
						}
						FD_CLR(i, &main_fd_set);
					}
				}
			}
		}
	}

	if (close (socket_fd) < 0) {
		syserr("close");
	}

	return 0;
}