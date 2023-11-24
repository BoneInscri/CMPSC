#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "channel.h"
#include "stress_send_recv.h"

static size_t num_channel;
static chan_t** channels;
static volatile atomic_bool done;
static chan_t* main_channel;

void* worker_thread(void* arg)
{
    size_t index = (size_t)arg;
    size_t next_index = index + 1;
    if (next_index >= num_channel) {
        next_index = 0;
    }
    chan_t* my_channel = channels[index];
    chan_t* next_channel = channels[next_index];
    bool start = true;
    enum chan_status status;
    while (true) {
        void* data = NULL;
        if (start) {
            status = channel_receive(main_channel, &data, true);
            assert(status == SUCCESS);
            if (data == NULL) {
                // indicates start period is over
                start = false;
                continue;
            }
        } else {
            status = channel_receive(my_channel, &data, true);
            assert(status == SUCCESS);
            if (data == NULL) {
                // indicates completion
                break;
            }
        }
        if (atomic_load(&done)) {
            // Send data to main_channel
            status = channel_send(main_channel, data, true);
            assert(status == SUCCESS);
        } else {
            // Pass along message to next thread in ring
            status = channel_send(next_channel, data, true);
            assert(status == SUCCESS);
        }
    }
    return NULL;
}

void run_stress_send_recv(size_t buffer_size, size_t num_threads, double load, useconds_t duration_usec)
{
    enum chan_status status;
    // setup
    num_channel = num_threads;
    atomic_store(&done, false);
    size_t num_msgs = (size_t)(((double)(num_channel * (buffer_size + 1))) * load);
    bool* msg_check = calloc(num_msgs + 1, sizeof(bool));
    assert(msg_check != NULL);

    channels = malloc(sizeof(chan_t*) * num_channel);
    assert(channels != NULL);
    for (size_t i = 0; i < num_channel; i++) {
        channels[i] = channel_create(buffer_size);
        assert(channels[i] != NULL);
    }
    main_channel = channel_create(buffer_size);
    assert(main_channel != NULL);

    pthread_t* pid = malloc(sizeof(pthread_t) * num_channel);
    assert(pid != NULL);
    for (size_t i = 0; i < num_channel; i++) {
        int pthread_status = pthread_create(&pid[i], NULL, worker_thread, (void*)i);
        assert(pthread_status == 0);
    }

    // start test
    for (size_t msg = 1; msg <= num_msgs; msg++) {
        // insert data into threads
        status = channel_send(main_channel, (void*)msg, true);
        assert(status == SUCCESS);
    }
    for (size_t i = 0; i < num_channel; i++) {
        // send start message
        status = channel_send(main_channel, NULL, true);
        assert(status == SUCCESS);
    }

    // wait for duration
    usleep(duration_usec);

    // stop test
    atomic_store(&done, true);
    for (size_t msg = 1; msg <= num_msgs; msg++) {
        // pull data from threads
        size_t data = 0;
        status = channel_receive(main_channel, (void**)&data, true);
        assert(status == SUCCESS);
        // check that data wasn't duplicated
        assert((1 <= data) && (data <= num_msgs));
        assert(msg_check[data] == false);
        msg_check[data] = true;
    }

    // shutdown
    for (size_t i = 0; i < num_channel; i++) {
        // send stop message
        status = channel_send(channels[i], NULL, true);
        assert(status == SUCCESS);
    }
    for (size_t i = 0; i < num_channel; i++) {
        // join threads
        pthread_join(pid[i], NULL);
    }

    // cleanup
    status = channel_close(main_channel);
    assert(status == SUCCESS);
    status = channel_destroy(main_channel);
    assert(status == SUCCESS);
    for (size_t i = 0; i < num_channel; i++) {
        status = channel_close(channels[i]);
        assert(status == SUCCESS);
        status = channel_destroy(channels[i]);
        assert(status == SUCCESS);
    }
    free(msg_check);
    free(pid);
    free(channels);
}
