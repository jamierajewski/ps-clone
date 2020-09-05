#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include "sort.h"

// This file contains the sorting aspect of the ps_server.
// The sort is done by creating a binary tree structure with each
// node containing a word that will be sorted on insertion.

int flag;
int global_count;

// Create a new node based on the given data
struct node *create(char *data)
{
	struct node *new_node = malloc(sizeof(*new_node));

	new_node->count = 1;
	new_node->data = malloc(sizeof(char) * PATH_MAX);
	strncpy(new_node->data, data, PATH_MAX);
	new_node->left = NULL;
	new_node->right = NULL;
	return new_node;
}

// Create a new link-list node
struct link_node *create_link(pthread_t * name)
{
	struct link_node *new_node = malloc(sizeof(*new_node));

	new_node->thread_ID = name;
	new_node->next = NULL;
	return new_node;
}

// Insert into the linked-list
struct link_node *insert_link(struct link_node *current, pthread_t * name)
{
	if (current == NULL) {
		current = create_link(name);
		return current;
	}

	struct link_node *head = create_link(name);
	head->next = current;
	return head;
}

//ORIGINAL
// Traverse the tree in-order and write each entry to the socket
void traverse(struct node *current)
{
	if (current == NULL)
		return;

	traverse(current->left);
	printf("%s\t%d\n", current->data, current->count);
	traverse(current->right);
}

// Join each thread in the linked list, then destroy
// the node
void join_list(struct link_node **current)
{
	// Iterate over the list until every element is gone including
	// the current
	struct link_node *head;
	while ((*current) != NULL) {
		pthread_join(*((*current)->thread_ID), NULL);	// **DEREFERENCE** //
		free((*current)->thread_ID);	// **FREE THE THREAD** //
		head = *current;
		*current = head->next;
		free(head);
	}
}

// Traverse the tree in-order and send each proc name followed
// by its count to the socket
void traverse_client(struct node *current, int client_sock, char *buf)
{
	if (current == NULL)
		return;

	traverse_client(current->left, client_sock, buf);
	memset(buf, 0, PATH_MAX);
	snprintf(buf, PATH_MAX, "%s\t%d\n", current->data, current->count);
	// Send the info
	if (send(client_sock, buf, strnlen(buf, PATH_MAX), 0) == -1) {
		perror("\nSEND ERROR - Couldn't send proc name");
		close(client_sock);
		pthread_exit(NULL);
		return;
	}
	traverse_client(current->right, client_sock, buf);
}

// Similar to traverse, but it counts the total number of processes
// in the tree
int total_count(struct node *current, int count)
{
	if (current == NULL) {
		return count;
	}
	count = total_count(current->left, count);
	count += current->count;
	count = total_count(current->right, count);
	return count;
}

// Return pointer to formatted header so that it can be printed
// to screen or logged to file
char *log_header(void)
{
	//Create time stamp
	time_t t = time(NULL);
	struct tm *info = localtime(&t);
	char *time_buf = malloc(sizeof(char) * 80);
	char *return_buf = malloc(sizeof(char) * 130);

	strftime(time_buf, 80, "%a %b %d %X %Z %Y", info);
	snprintf(return_buf, 130, "\nPS Server logging started %s", time_buf);
	free(time_buf);
	return return_buf;
}

// Insert data into tree at its appropriate position
struct node *insert(struct node *current, char *data)
{
	if (!(isalpha(data[0])) && flag == 0) {
		data++;
		flag = 1;
	}
	// Check if current node is NULL; if it is, put the new node here
	// and return since we're done
	if (current == NULL) {
		if (flag == 1) {
			--data;
			flag = 0;
		}
		current = create(data);
		return current;
	}
	// Otherwise, recursively check both the left and right to find a spot
	else if (strncmp(current->data, data, strnlen(data, NAME_MAX)) > 0)
		current->left = insert(current->left, data);

	else if (strncmp(current->data, data, strnlen(data, NAME_MAX)) < 0)
		current->right = insert(current->right, data);

	// If the nodes are the same, increment the count of current
	else {
		current->count += 1;
	}

	// Return root
	return current;
}

// Free up each node of the tree recursively
void destroy(struct node *current)
{
	if (current != NULL) {
		destroy(current->left);
		destroy(current->right);
		// Free after recursively accessing the children nodes so as to
		// prevent hanging pointers
		free(current->data);
		free(current);
	}
}
