#include "buffer.h"

// Creates a buffer with the given capacity
buffer_t* buffer_create(size_t capacity)
{
    buffer_t* buffer = (buffer_t*) malloc(sizeof(buffer_t));
    void** data  = (void**) malloc(capacity * sizeof(void*));
    buffer->size = 0;
    buffer->next = 0;
    buffer->capacity = capacity;
    buffer->data = data;
    return buffer;
}

// Adds the value into the buffer
// Returns 'true' if the buffer is not full and a value was added
// Returns 'false' otherwise
bool buffer_add(void* data, buffer_t* buffer)
{
    if (buffer->size >= buffer->capacity) {
        return false;
    }
    size_t pos = buffer->next + buffer->size;
    if (pos >= buffer->capacity) {
        pos -= buffer->capacity;
    }
    buffer->data[pos] = data;
    buffer->size++;
    return true;
}

// Removes the value from the buffer in FIFO order and stores it in data
// Returns a value if the buffer is not empty and the value was removed
// Returns BUFFER_EMPTY otherwise
void *buffer_remove(buffer_t* buffer)
{
    if (buffer->size > 0) {
        void *data = buffer->data[buffer->next];
        buffer->size--;
        buffer->next++;
        if (buffer->next >= buffer->capacity) {
            buffer->next -= buffer->capacity;
        }
        return data;
    }
    return BUFFER_EMPTY;
}

// Frees the memory allocated to the buffer
void buffer_free(buffer_t *buffer)
{
    free(buffer->data);
    free(buffer);
}

// Returns the total capacity of the buffer
size_t buffer_capacity(buffer_t* buffer)
{
    return buffer->capacity;
}

// Returns the current number of elements in the buffer
size_t buffer_current_size(buffer_t* buffer)
{
    return buffer->size;
}

// Peeks at a value in the buffer
// Only used for testing code; you should NOT use this
void* peek_buffer(size_t index, buffer_t* buffer)
{
    return buffer->data[index];
}
