#ifndef sort_H
#define sort_H

#include <pthread.h>

struct node {
	char *data;
	int count;
	struct node *left;
	struct node *right;
};

struct link_node {
	pthread_t *thread_ID;
	struct link_node *next;
};

// Prototypes for tree-related functions

struct node *create(char *data);

struct node *insert(struct node *current, char *data);

void destroy(struct node *current);

void traverse(struct node *current);

void traverse_client(struct node *current, int client_sock, char *buf);

char *log_header(void);

int total_count(struct node *current, int count);

struct link_node *create_link(pthread_t * name);

struct link_node *insert_link(struct link_node *current, pthread_t * name);

void join_list(struct link_node **current);

#endif
