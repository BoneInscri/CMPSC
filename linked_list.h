#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdlib.h>
#include <stddef.h>

typedef struct list_node {
    struct list_node* next;
    struct list_node* prev;
    void* data;
} list_node_t;

typedef struct {
    list_node_t head;// not use pointer; instead a structure
    size_t count;
} list_t;

// is empty ?
int list_empty(list_node_t *node);

// Creates and returns a new list
list_t* list_create();

// Destroys a list
void list_destroy(list_t* list, int alloc, int free_head);

// Returns beginning of the list
list_node_t* list_begin(list_t* list);

// Returns next element in the list
list_node_t* list_next(list_node_t* node);

// Returns data in the given list node
void* list_data(list_node_t* node);

// Returns the number of elements in the list
size_t list_count(list_t* list);

// Finds the first node in the list with the given data
// Returns NULL if data could not be found
list_node_t* list_find(list_t* list, void* data);

// Inserts a new node in the list with the given data
void list_insert(list_t* list, void* data);

// Removes a node from the list and frees the node resources
void list_remove(list_t* list, list_node_t* node, int alloc);

// Executes a function for each element in the list
void list_foreach_safe(list_t* list, void (*func)(void* data));

#endif // LINKED_LIST_H
