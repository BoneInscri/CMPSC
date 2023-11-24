#include "linked_list.h"
#include <assert.h>

// functions below are similar to list.h in Linux kernel
static void list_init(list_node_t *node)
{
    node->prev = node;
    node->next = node;
}

static void list_append(list_t *list, list_node_t *node)
{
    list->head.prev->next = node;
    node->prev = list->head.prev;
    list->head.prev = node;
    node->next = &list->head;
}

int list_empty(list_node_t *node)
{
    return node->next == node;
}

// Creates and returns a new list
list_t *list_create()
{
    list_t *list_new = (list_t *)malloc(sizeof(list_t));
    assert(list_new != NULL);
    list_new->count = 0;
    list_init(&(list_new->head));
    return list_new;
}

// Destroys a list
void list_destroy(list_t *list, int alloc, int free_head)
{
    // alloc : data is from malloc?
    // free_head : free list_head ?
    list_node_t *list_node_tmp = list_begin(list);
    while (!list_empty(&list->head))
    {
        list_remove(list, list_node_tmp, alloc);
        list_node_tmp = list_begin(list); // we delete the first node every time!
    }
    if(free_head) {
        free(list);
    }
}

// Returns beginning of the list
list_node_t *list_begin(list_t *list)
{
    return list->head.next;
}

// Returns next element in the list
list_node_t *list_next(list_node_t *node)
{
    return node->next;
}

// Returns data in the given list node
void *list_data(list_node_t *node)
{
    return node->data;
}

// Returns the number of elements in the list
size_t list_count(list_t *list)
{
    return list->count;
}

// Finds the first node in the list with the given data
// Returns NULL if data could not be found
list_node_t *list_find(list_t *list, void *data)
{
    list_node_t *list_node_tmp = list_begin(list);
    while (list_node_tmp != &list->head)
    {
        if (list_data(list_node_tmp) == data)
        {
            return list_node_tmp;
        }
        list_node_tmp = list_next(list_node_tmp);
    }
    return NULL;
}

// Inserts a new node in the list with the given data
void list_insert(list_t *list, void *data)
{
    list_node_t *list_node_new = (list_node_t *)malloc(sizeof(list_node_t));
    assert(list_node_new != NULL);
    list_node_new->data = data;
    list_init(list_node_new);
    list_append(list, list_node_new);
    list->count++;
}

// Removes a node from the list and frees the node resources
void list_remove(list_t *list, list_node_t *node, int alloc)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    // may need to free node->data? if data is from malloc, we have to
    if(alloc) {
        free(node->data);
    }
    free(node);
    list->count--;
    assert((list->count) >= 0);
}

// Executes a function for each element in the list
void list_foreach_safe(list_t *list, void (*func)(void *data))
{
    list_node_t *list_node_tmp = list_begin(list);
    while (list_node_tmp != &list->head) // we make this list as bidirectional circular linked list
    {
        list_node_t* list_node_next = list_next(list_node_tmp);
        (*func)(list_node_tmp->data);
        list_node_tmp = list_node_next;// safe !
    }
}