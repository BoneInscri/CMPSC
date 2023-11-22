#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    size_t size;
    size_t next;
    size_t capacity;
    void** data;
} buffer_t;

#define BUFFER_EMPTY	((void *) -1L)

// Creates a buffer with the given capacity
buffer_t* buffer_create(size_t capacity);

// Adds the value into the buffer
// Returns 'true' if the buffer is not full and a value was added
// Returns 'false' otherwise
bool buffer_add(void* data, buffer_t* buffer);

// Removes the value from the buffer in FIFO order and stores it in data
// Returns a value if the buffer is not empty and the value was removed
// Returns BUFFER_EMPTY otherwise
void *buffer_remove(buffer_t* buffer);

// Frees the memory allocated to the buffer
void buffer_free(buffer_t* buffer);

// Returns the total capacity of the buffer
size_t buffer_capacity(buffer_t* buffer);

// Returns the current number of elements in the buffer
size_t buffer_current_size(buffer_t* buffer);

// Peeks at a value in the buffer
// Only used for testing code; you should NOT use this
void* peek_buffer(size_t index, buffer_t* buffer);

#endif // BUFFER_H
