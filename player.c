#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "err.h"

#define FALSE 0
#define TRUE 1

#define CONST_SIZE 16
#define STDOUT_FD 1
#define CR_SIZE 4
#define WAIT_SEC 5

#define BUFFER_SIZE 1024

static const char sign_string[] = "-";
static const char mp3_string[] = ".mp3";
static const char no_string[] = "no";
static const char yes_string[] = "yes";

static const char icy_ok_string[] = "ICY 200 OK";
static const char icy_metaint_string[] = "icy-metaint:";
static const char icy_br_string[] = "icy-br:";
static const char cr_string[] = "\r\n\r\n";
static const char stream_title_string[] = "StreamTitle";

static const char pause_string[] = "PAUSE";
static const char play_string[] = "PLAY";
static const char title_string[] = "TITLE";
static const char quit_string[] = "QUIT";

static char meta_data_string[] = "Icy-MetaData:1\r\n";
static char empty_string[] = "";
static char message_fmt[] = "GET %s HTTP/1.0\r\n%s\r\n";


void connect_with_radio_server(int *socket_fd, struct sockaddr_in *server, char* host, long port) 
{
	struct hostent *h;

	struct timeval timeout;      
	timeout.tv_sec = WAIT_SEC;
	timeout.tv_usec = 0;

	if ((*socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		syserr("socket");

	if ((h = gethostbyname(host)) == NULL)
		syserr("gethostbyname");

	memcpy(&server->sin_addr, h->h_addr_list[0], h->h_length);
	server->sin_family = AF_INET;
	server->sin_port = htons(port);

	memset(server->sin_zero, '\0', sizeof server->sin_zero);

	if (setsockopt (*socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		syserr("setsockopt");

	if (setsockopt (*socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		syserr("setsockopt");

	if (connect(*socket_fd, (struct sockaddr *)server, sizeof(struct sockaddr)) == -1)
		syserr("connect");
}

void connect_udp(int *socket_fd, struct sockaddr_in *server, long port) 
{
	if ((*socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		syserr("socket");

	server->sin_family = AF_INET;
	server->sin_addr.s_addr = htonl(INADDR_ANY);
	server->sin_port = htons(port);

	memset(server->sin_zero, '\0', sizeof server->sin_zero);

	if (bind(*socket_fd, (struct sockaddr *)server, sizeof(struct sockaddr)) < 0) 
		syserr("bind");

	if (fcntl(*socket_fd, F_SETFL, O_NONBLOCK) < 0)
		syserr("fcntl");
}

int starts_with(const char *word, const char *starter)
{
	if (strncmp(word, starter, strlen(starter)) == 0)
		return TRUE;
	return FALSE;
}

int equals(const char* left_word, const char* right_word)
{
	if (strcmp(left_word, right_word) == 0)
		return TRUE;
	return FALSE;
}

void save_to_variable(char* variable, char *arg)
{
	memset(variable, 0, BUFFER_SIZE);
	strncpy(variable, arg, strlen(arg));
}

void save_port_number(long *port, char *arg)
{
	char* rest;

	*port = strtol(arg, &rest, 10);
	if ((rest == arg) || (*rest != '\0'))
		fatal("'%s' is not valid port number\n", arg);
	if (*port < 0)
		fatal("'%s' is not valid port number\n", arg);
	if (*port > 65535)
		fatal("'%s' is not valid port number\n", arg);
}

void save_write_fd(int *fd, char *arg)
{
	if (strcmp(arg, sign_string) == 0)
		*fd = STDOUT_FD;
	else if(strstr(arg, mp3_string) != NULL)
		*fd = open(arg, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IXUSR);
	else
		fatal("'%s' is not valid file name\n", arg);
}

void send_request(int *socket_fd, char *get_request)
{
	int sent, total, bytes;

	total = strlen(get_request);
	sent = 0;
	do {
		bytes = write(*socket_fd, get_request + sent, total - sent);
		if (bytes < 0)
			syserr("write");
		if (bytes == 0)
			break;
		sent += bytes;
	} while (sent < total);
}

int save_meta_data(char* meta_data, char* arg)
{
	if (strcmp(arg, no_string) == 0) {
		save_to_variable(meta_data, empty_string);
		return TRUE;
	} else if (strcmp(arg, yes_string) == 0)
		save_to_variable(meta_data, meta_data_string);
	else
		fatal("'%s' is not valid md option\n", arg);
	return FALSE;
}

void write_data(int write_fd, char *response, int bytes)
{
	void *p = response;

	while (bytes > 0) {
		int bytes_written = write(write_fd, p, bytes);
		if (bytes_written <= 0)
			syserr("write");
		bytes -= bytes_written;
		p += bytes_written;
	}
}

void save_song_title(char *song_title, char *tmp)
{
	char *separator;
	char *fragment;

	/* bo w tytule moze byc znak ' np: David Bowie - Let's Dance */
	separator = "=;";
	fragment = strtok(tmp, separator);
	fragment = strtok(NULL, separator);
	memset(song_title, 0, BUFFER_SIZE);
	strncat(song_title, fragment + 1, strlen(fragment) - 2);
}

int main(int argc, char *argv[])
{
	int request_approved = FALSE;
	int metaint_known = FALSE;
	int meta_data_present = TRUE;
	int already_file = FALSE;

	int play = TRUE;
	int quit = FALSE;
	char song_title[BUFFER_SIZE];
	
	char host[BUFFER_SIZE], path[BUFFER_SIZE];
	long r_port, m_port;
	struct sockaddr_in radio_server, udp_server, udp_client;
	int radio_socket_fd, udp_socket_fd, write_fd;
	char radio_response[BUFFER_SIZE], udp_response[BUFFER_SIZE];
	int radio_bytes, udp_bytes_recv, udp_bytes_send;
	socklen_t rcva_len, snda_len;

	int received, chunk_size, length, left;
	char get_request[BUFFER_SIZE], tmp[BUFFER_SIZE], meta_data[BUFFER_SIZE];
	char *separator;
	char *fragment;
	char *rest;

	if (argc != 7)
		fatal("Usage: %s host path r-port file m-port md\n", argv[0]);

	save_to_variable(host, argv[1]);
	save_to_variable(path, argv[2]);
	save_port_number(&r_port, argv[3]);
	save_write_fd(&write_fd, argv[4]);
	save_port_number(&m_port, argv[5]);
	metaint_known = save_meta_data(meta_data, argv[6]);
	meta_data_present = !metaint_known;

	connect_with_radio_server(&radio_socket_fd, &radio_server, host, r_port);
	sprintf(get_request, message_fmt, path, meta_data);
	send_request(&radio_socket_fd, get_request);

	connect_udp(&udp_socket_fd, &udp_server, m_port);

	received = 0;
	chunk_size = 0;

	while (!quit) {
		radio_bytes = read(radio_socket_fd, radio_response, BUFFER_SIZE);
		if (radio_bytes <= 0) {
			quit = TRUE;
			continue;
		}
		if (play) {
			if (!request_approved) {
				if (starts_with(radio_response, icy_ok_string))
					request_approved = TRUE;
				else
					fatal("'%s' not received", icy_ok_string);
			}
			if (!metaint_known) {
				if ((rest = strstr(radio_response, icy_metaint_string))) {
					strncpy(tmp, rest, sizeof(tmp));
					metaint_known = TRUE;
					separator = ":\r\n";
					fragment = strtok(tmp, separator);
					fragment = strtok(NULL, separator);
					chunk_size = strtol(fragment, &rest, 10);
					if ((rest == fragment) || (*rest != '\0'))
						fatal("'%s' is not valid icy metaint number\n", fragment);
					if (chunk_size < 0)
						fatal("'%s' is not valid icy metaint number\n", fragment);
				}
			}
			if (metaint_known && !already_file) {
				if ((rest = strstr(radio_response, cr_string))) {
					strncpy(tmp, rest, sizeof(tmp));
					already_file = TRUE;
					fragment = tmp + CR_SIZE;
					received += strlen(fragment);
					write_data(write_fd, fragment, strlen(fragment));
					continue;
				}
			}
			if (request_approved && metaint_known && already_file) {
				if (!meta_data_present) {
					received += radio_bytes;
					write_data(write_fd, radio_response, radio_bytes);
				} else {
					if (received + radio_bytes <= chunk_size) {
						/* czasami jest wczesniej */
						if ((rest = strstr(radio_response, stream_title_string))) {
							length = radio_response[0];
							left = 1;
							radio_bytes -= 1;
							strncpy(tmp, radio_response + left, length*CONST_SIZE);
							save_song_title(song_title, tmp);
							radio_bytes -= length*CONST_SIZE;
							received += radio_bytes;
							if(radio_bytes > 0)
								write_data(write_fd, radio_response, radio_bytes);
							received = 0;
						} else {
							received += radio_bytes;	
							write_data(write_fd, radio_response, radio_bytes);
						}
					} else {
						left = chunk_size - received;
						radio_bytes -= left;
						strncpy(fragment, radio_response, left);
						received += strlen(fragment);
						write_data(write_fd, fragment, strlen(fragment));
						strncpy(fragment, empty_string, strlen(empty_string));
						strncpy(fragment, radio_response + left, 1);
						length = fragment[0];
						if (length >= 0) {
							strncpy(tmp, radio_response + left + 1, length*CONST_SIZE);
							/* dla pewnosci */
							if ((rest = strstr(tmp, stream_title_string))) {
								radio_bytes -= 1;
								left += 1;
								save_song_title(song_title, tmp);
								radio_bytes -= length*CONST_SIZE;
								left += length*CONST_SIZE;
							}
						}
						received += radio_bytes;
						if(radio_bytes > 0)
							write_data(write_fd, radio_response + left, radio_bytes);
						received = 0;
					}
				}
			}
		}

		memset(radio_response, 0, sizeof(radio_response));
		memset(tmp, 0, sizeof(tmp));
		strncpy(fragment, empty_string, strlen(empty_string));
		strncpy(rest, empty_string, strlen(empty_string));	
		memset(udp_response, 0, sizeof(udp_response));

		snda_len = (socklen_t) sizeof(udp_client);
		rcva_len = (socklen_t) sizeof(udp_client);

		udp_bytes_recv = recvfrom(udp_socket_fd, udp_response, BUFFER_SIZE, 0,
					(struct sockaddr *) &udp_client, &rcva_len);

		if (udp_bytes_recv > 0) {
			if (equals(udp_response, title_string)) {
				length = strlen(song_title);
				udp_bytes_send = sendto(udp_socket_fd, song_title, (size_t) length, 0,
						(struct sockaddr *) &udp_client, snda_len);
				if (udp_bytes_send != length)
					syserr("sendto");
			} else if (equals(udp_response, play_string)) {
				play = TRUE;
			} else if (equals(udp_response, pause_string)) {
				play = FALSE;
				received = 0;
			} else if (equals(udp_response, quit_string)) {
				quit = TRUE;
			} else {
				bad("'%s' is not valid option\n", udp_response);
			}
		}
	}

	if (radio_bytes < 0)
		syserr("read");

	if (close(radio_socket_fd) < 0)
		syserr("close");

	if (close(udp_socket_fd) < 0)
		syserr("close");

	if (close(write_fd) < 0)
		syserr("close");

	return 0;
}
