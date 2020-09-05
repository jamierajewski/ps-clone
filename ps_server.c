#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libgen.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include "sort.h"

// Simple struct that packs arguments to pass to thread
struct package {
	pthread_t *thread;
	int client_sock;
};

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
struct link_node *head;

// NONE OF THESE NEED TO BE THREAD-SAFE SINCE THEY ARE
// ASSIGNED TO ONCE BY THE MAIN THREAD THEN READ THEREAFTER
int sig_flag;
char *filename;
int server_sock;

// Returns UID for user; -1 if failed
long get_uid(char *username)
{
	struct passwd *user = getpwnam(username);

	if (user == NULL)
		return -1;
	long uid = (long)user->pw_uid;
	return uid;
}

// Checks if string is only numbers
int num_checker(char *dir_name)
{
	int i;

	for (i = 0; i < strnlen(dir_name, PATH_MAX); i++) {
		if (!(dir_name[i] >= '0' && dir_name[i] <= '9'))
			return 1;
	}
	return 0;
}

// Compare the given UID to the one in the status file of 'path';
// If its the same, return the associated process name
char *proc_checker(char *path, char *username)
{
	FILE *fp;
	int user_UID = get_uid(username);
	int stored_UID;
	char *line = NULL;
	size_t len = 0;
	char *proc_name = malloc(sizeof(char) * PATH_MAX);

	if (user_UID == -1)
		return NULL;

	fp = fopen(path, "r");
	if (fp != NULL) {
		while (getline(&line, &len, fp) != -1) {
			if (sscanf(line, "Name:  %s", proc_name) == 1);
			if (sscanf(line, "Uid:     %d", &stored_UID) == 1);
		}
		fclose(fp);
		free(line);
		if (stored_UID == user_UID)
			return proc_name;
		free(proc_name);
		return NULL;
	}
	free(proc_name);
	return NULL;
}

// Runs through /proc folder, checking
// processes that match the given username.
// Returns root of tree created for user.
struct node *proc_skimmer(char *username, struct node *root)
{
	DIR *stream;
	struct dirent *dp;
	char *path = malloc(sizeof(char) * PATH_MAX);
	char *result;

	stream = opendir("/proc");

	if (stream == NULL) {
		perror("Cannot open /proc");
		exit(1);
	}
	// Now reading through /proc
	while ((dp = readdir(stream)) != NULL) {
		memset(path, 0, PATH_MAX);
		if (dp->d_type == DT_DIR && num_checker(dp->d_name) == 0) {
			// A process was found, extract info
			strncpy(path, "/proc/", 6);
			strncat(path, dp->d_name, PATH_MAX);
			strncat(path, "/status", 7);
			result = proc_checker(path, username);
			if (result) {
				root = insert(root, result);
				free(result);
			}
		}
	}
	free(path);
	closedir(stream);
	return root;
}

// Take the username and run the proc_skimmer
// to find which are associated to the user;
// send results back over through the socket
void server_reply(char *username, int client_sock)
{

	char *buf = malloc(sizeof(char) * PATH_MAX);
	int count = 0;
	struct node *root = NULL;
	// Create tree full of procs and counts
	root = proc_skimmer(username, root);
	// Traverse it in-order, sending name followed by count to client
	traverse_client(root, client_sock, buf);
	free(buf);
	count = total_count(root, count);
	//Before destroying the tree, do logging
	if (filename == NULL) {
		printf("USER %s [%d processes]\n", username, count);
	} else {
		pthread_mutex_lock(&file_mutex);
		FILE *fp = fopen(filename, "a");

		fprintf(fp, "USER %s [%d processes]\n", username, count);
		fclose(fp);
		pthread_mutex_unlock(&file_mutex);
	}
	// Destroy tree to free memory (and reset global_count)
	destroy(root);
}

// What the server does after SIGINT has been received and the
// flag has been flipped by the handle() function
void handle_action(void)
{
	// Clean up remaining threads
	join_list(&head);
	printf("\nServer successfully stopped.\n");
	close(server_sock);
}

// Loop on this function within the server code to continue
// accepting connections
void *server_accept(void *package)
{
	// Unpack the struct
	struct package local_pkg = *((struct package *)package);
	int client_sock = local_pkg.client_sock;
	pthread_t *thread = local_pkg.thread;

	int bytes_rec;
	char *buf = malloc(sizeof(char) * PATH_MAX);

	memset(buf, 0, PATH_MAX);

	bytes_rec = recv(client_sock, buf, PATH_MAX, 0);
	strtok(buf, "\n");
	if (bytes_rec == -1) {
		perror("\nRECV ERROR");
		close(client_sock);
	}

	server_reply(buf, client_sock);
	free(buf);
	close(client_sock);

	free(package);

	pthread_mutex_lock(&list_mutex);
	head = insert_link(head, thread);
	pthread_mutex_unlock(&list_mutex);

	return NULL;
}

// Handler for SIGINT; simply flip a global flag to
// indicate that the signal was received
void handle(int sig)
{
	sig_flag = 1;
}

// Primary server code; sets up the server and loops the
// server_accept() function until SIGINT is received
void server(char *sock_path)
{
	int client_sock, rc;
	socklen_t len;
	char *header = NULL;
	int backlog = 100;
	struct sockaddr_un server_sockaddr;
	struct sockaddr_un client_sockaddr;

	memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
	memset(&client_sockaddr, 0, sizeof(struct sockaddr_un));

	header = log_header();

	if (filename == NULL) {
		printf("%s\n", header);
	} else {
		FILE *fp = fopen(filename, "a");

		fprintf(fp, "%s\n", header);
		fclose(fp);
	}
	free(header);
	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_sock == -1) {
		perror("\nSOCKET ERROR");
		exit(1);
	}
	server_sockaddr.sun_family = AF_UNIX;
	strncpy(server_sockaddr.sun_path, sock_path,
		strnlen(sock_path, PATH_MAX));
	len = sizeof(server_sockaddr);

	unlink(sock_path);
	rc = bind(server_sock, (struct sockaddr *)&server_sockaddr, len);
	if (rc == -1) {
		perror("\nBIND ERROR");
		close(server_sock);
		exit(1);
	}

	rc = listen(server_sock, backlog);
	if (rc == -1) {
		perror("\nLISTEN ERROR");
		close(server_sock);
		exit(1);
	}

	while (sig_flag == 0) {
		client_sock = accept(server_sock, (struct sockaddr *)
				     &client_sockaddr, &len);
		if (client_sock == -1) {
			;
		} else {
			struct package *thread_pack =
			    malloc(sizeof(*thread_pack));
			pthread_t *thread = malloc(sizeof(pthread_t));
			thread_pack->thread = thread;
			thread_pack->client_sock = client_sock;
			pthread_create(thread, 0, server_accept,
				       (void *)(thread_pack));
		}
		pthread_mutex_lock(&list_mutex);
		join_list(&head);
		pthread_mutex_unlock(&list_mutex);
	}
	// SIGINT was received
	handle_action();
}

int main(int argc, char *argv[])
{
	int opt;
	struct sigaction sa;
	char *usage = malloc(PATH_MAX);

	strncpy(usage, "\nUsage: ps_server socketname [-v filename]\n", 45);

	sa.sa_handler = handle;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGINT, &sa, NULL) == -1) {
		perror("\nSIGNAL ERROR");
		exit(1);
	}
	if (argc == 4) {
		while ((opt = getopt(argc, argv, "v:")) != -1) {
			switch (opt) {
			case 'v':
				if (argv[1] == NULL) {
					printf("%s", usage);
					exit(1);
				}
				filename = optarg;
				server(argv[1]);
				break;
			case '?':
				printf("\nInvalid argument.\n");
				exit(1);
			}
		}
	} else if (argc == 2) {
		server(argv[1]);
	} else {
		printf("%s", usage);
	}
	free(usage);
	return 0;
}
