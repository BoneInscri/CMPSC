#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include "buffer.h"
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "linked_list.h"

// for send、receive and select 
#define SEND 0
#define RECV 1
#define SELECT_ 3

// select lists
#define SELECT_RECV 0
#define SELECT_SEND 1

// atomic
#define Channel_Lock(channel) (pthread_mutex_lock(&((channel)->mutex)))
// #define Channel_TryLock(channel) (pthread_mutex_trylock(&((channel)->mutex)))
#define Channel_Unlock(channel) (pthread_mutex_unlock(&((channel)->mutex)))


#define Mysema_Lock(sema) (pthread_mutex_lock(&((sema)->count_mutex)))
#define Mysema_Unlock(sema) (pthread_mutex_unlock(&((sema)->count_mutex)))

// send sema
#define Channel_Send_Sema_wait(channel) (channel_sema_wait(channel, SEND))
#define Channel_Send_Sema_signal(channel) (channel_sema_signal(channel, SEND))
#define Channel_Send_Sema_broadcast(channel) (channel_sema_broadcast(channel, SEND))

// select send list
#define Channel_Select_Send_Sema_signal(channel) (channel_select_signal(channel, SELECT_SEND))
#define Channel_Select_Send_Sema_broadcast(channel) (channel_select_broadcast(channel, SELECT_SEND))

// recv sema
#define Channel_Recv_Sema_wait(channel) (channel_sema_wait(channel, RECV))
#define Channel_Recv_Sema_signal(channel) (channel_sema_signal(channel, RECV))
#define Channel_Recv_Sema_broadcast(channel) (channel_sema_broadcast(channel, RECV))

// select recv list
#define Channel_Select_Recv_Sema_signal(channel) (channel_select_signal(channel, SELECT_RECV))
#define Channel_Select_Recv_Sema_broadcast(channel) (channel_select_broadcast(channel, SELECT_RECV))

// closed ?
#define Channel_is_closed(channel) (channel->closed) 
#define Channel_set_closed(channel) (channel->closed = 1)

typedef struct 
{
    sem_t sema;
    int count;
    pthread_mutex_t count_mutex;
} my_sema;

// Defines possible return values from channel functions
enum chan_status
{
    SUCCESS = 1,
    WOULDBLOCK = 0,
    OTHER_ERROR = -1,
    CLOSED_ERROR = -2,
    DESTROY_ERROR = -3
};

// Defines channel object
typedef struct
{
    // DO NOT REMOVE buffer (OR CHANGE ITS NAME) FROM THE STRUCT
    // YOU MUST USE buffer TO STORE YOUR BUFFERED CHANNEL MESSAGES
    buffer_t *buffer;

    /* ADD ANY STRUCT ENTRIES YOU NEED HERE */
    pthread_mutex_t mutex;
    int closed;
    my_sema* sema[2]; // sema[0] : send , sema[1] : recv
    list_t *select_sema_list[2]; // for select , list[0] : send_list, list[1] : recv_list
} chan_t;

typedef struct
{
    // Channel on which we want to perform operation
    chan_t *channel;
    // Specified whether we want to send (SEND): true, or receive (RECV): false on the channel
    bool is_send;
    // If is_send = false (RECV), then the message received from the channel is stored as an output in this parameter, data
    // If is_send = true (SEND), then the message that needs to be sent is given as input in this parameter, data
    void *data;
} select_t;

// Creates a new channel with the provided size and returns it to the caller
// A 0 size indicates an unbuffered channel, whereas a positive size indicates a buffered channel
chan_t *channel_create(size_t size);

// Writes data to the given channel
// This can be both a blocking call i.e., the function only returns on a successful completion of send (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is full (blocking = false)
// In case of the blocking call when the channel is full, the function waits till the channel has space to write the new data
// Returns SUCCESS for successfully writing data to the channel,
// WOULDBLOCK if the channel is full and the data was not added to the buffer (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_send(chan_t *channel, void *data, bool blocking);

// Reads data from the given channel and stores it in the function’s input parameter, data (Note that it is a double pointer).
// This can be both a blocking call i.e., the function only returns on a successful completion of receive (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is empty (blocking = false)
// In case of the blocking call when the channel is empty, the function waits till the channel has some data to read
// Returns SUCCESS for successful retrieval of data,
// WOULDBLOCK if the channel is empty and nothing was stored in data (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_receive(chan_t *channel, void **data, bool blocking);

// Closes the channel and informs all the blocking send/receive/select calls to return with CLOSED_ERROR
// Once the channel is closed, send/receive/select operations will cease to function and just return CLOSED_ERROR
// Returns SUCCESS if close is successful,
// CLOSED_ERROR if the channel is already closed, and
// OTHER_ERROR in any other error case
enum chan_status channel_close(chan_t *channel);

// Frees all the memory allocated to the channel
// The caller is responsible for calling channel_close and waiting for all threads to finish their tasks before calling channel_destroy
// Returns SUCCESS if destroy is successful,
// DESTROY_ERROR if channel_destroy is called on an open channel, and
// OTHER_ERROR in any other error case
enum chan_status channel_destroy(chan_t *channel);

// Takes an array of channels, channel_list, of type select_t and the array length, channel_count, as inputs
// This API iterates over the provided list and finds the set of possible channels which can be used to invoke the required operation (send or receive) specified in select_t
// If multiple options are available, it selects the first option and performs its corresponding action
// If no channel is available, the call is blocked and waits till it finds a channel which supports its required operation
// Once an operation has been successfully performed, select should set selected_index to the index of the channel that performed the operation and then return SUCCESS
// In the event that a channel is closed or encounters any error, the error should be propagated and returned through select
// Additionally, selected_index is set to the index of the channel that generated the error
enum chan_status channel_select(size_t channel_count, select_t *channel_list, size_t *selected_index);

#endif // CHANNEL_H
