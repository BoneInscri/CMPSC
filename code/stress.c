#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "channel.h"
#include "stress.h"

typedef unsigned int distance_t;
typedef struct {
    size_t src;
    size_t epoch;
    distance_t dist[0];
} distance_vector_t;

static const distance_t inf_distance = 0x7fffffff;
static distance_t* topology;
static distance_t* solution;
static size_t num_channel;
static chan_t** channels;
static chan_t* done_channel;
static chan_t* completed_channel;

distance_t get_link_distance(size_t src, size_t dst) {
    return topology[src * num_channel + dst];
}

void set_link_distance(size_t src, size_t dst, distance_t distance) {
    topology[src * num_channel + dst] = distance;
}

distance_t get_solution_distance(size_t src, size_t dst) {
    return solution[src * num_channel + dst];
}

void set_solution_distance(size_t src, size_t dst, distance_t distance) {
    solution[src * num_channel + dst] = distance;
}

void floyd_warshall()
{
    memcpy(solution, topology, sizeof(distance_t) * num_channel * num_channel);
    for (size_t intermediate = 0; intermediate < num_channel; intermediate++) {
        for (size_t src = 0; src < num_channel; src++) {
            for (size_t dst = 0; dst < num_channel; dst++) {
                if (get_solution_distance(src, intermediate) + get_solution_distance(intermediate, dst) < get_solution_distance(src, dst)) {
                    set_solution_distance(src, dst, get_solution_distance(src, intermediate) + get_solution_distance(intermediate, dst));
                }
            }
        }
    }
}

void print_graph()
{
    printf("GRAPH\n");
    for (size_t src = 0; src < num_channel; src++) {
        for (size_t dst = 0; dst < num_channel; dst++) {
            distance_t distance = get_link_distance(src, dst);
            if (distance == inf_distance) {
                printf("inf ");
            } else {
                printf("%03u ", distance);
            }
        }
        printf("\n");
    }
}

void print_solution()
{
    printf("SOLUTION\n");
    for (size_t src = 0; src < num_channel; src++) {
        for (size_t dst = 0; dst < num_channel; dst++) {
            distance_t distance = get_solution_distance(src, dst);
            if (distance == inf_distance) {
                printf("inf ");
            } else {
                printf("%03u ", distance);
            }
        }
        printf("\n");
    }
}

bool create_topology(const char* filename)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        printf("Could not open topology file: %s\n", filename);
        return false;
    }
    int num_scanned = fscanf(file, "%zu", &num_channel);
    assert(num_scanned == 1);
    assert(num_channel > 0);
    topology = malloc(sizeof(distance_t) * num_channel * num_channel);
    assert(topology != NULL);
    solution = malloc(sizeof(distance_t) * num_channel * num_channel);
    assert(solution != NULL);
    // populate topology
    for (size_t src = 0; src < num_channel; src++) {
        for (size_t dst = 0; dst < num_channel; dst++) {
            distance_t distance;
            num_scanned = fscanf(file, "%d", (int*)&distance);
            assert(num_scanned == 1);
            // negative values get converted to inf_distance
            if (distance > inf_distance) {
                distance = inf_distance;
            }
            set_link_distance(src, dst, distance);
        }
    }
    fclose(file);
    // calculate solution using Floyd-Warshall algorithm
    floyd_warshall();
    return true;
}

void destroy_topology()
{
    free(topology);
    free(solution);
}

void* router(void* arg)
{
    bool changed = false;
    size_t index = (size_t)arg;
    size_t selected_index;
    distance_vector_t* prev_prev_state = malloc(sizeof(distance_vector_t) + sizeof(distance_t) * num_channel);
    assert(prev_prev_state != NULL);
    distance_vector_t* prev_state = malloc(sizeof(distance_vector_t) + sizeof(distance_t) * num_channel);
    assert(prev_state != NULL);
    distance_vector_t* curr_state = malloc(sizeof(distance_vector_t) + sizeof(distance_t) * num_channel);
    assert(curr_state != NULL);
    distance_vector_t* next_state = malloc(sizeof(distance_vector_t) + sizeof(distance_t) * num_channel);
    assert(next_state != NULL);
    prev_prev_state->src = index;
    prev_state->src = index;
    curr_state->src = index;
    next_state->src = index;
    prev_prev_state->epoch = 0;
    prev_state->epoch = 1;
    curr_state->epoch = 2;
    next_state->epoch = 3;
    for (size_t i = 0; i < num_channel; i++) {
        prev_prev_state->dist[i] = get_link_distance(index, i);
        prev_state->dist[i] = get_link_distance(index, i);
        curr_state->dist[i] = get_link_distance(index, i);
        next_state->dist[i] = get_link_distance(index, i);
    }
    size_t total_select_count = 2;
    for (size_t i = 0; i < num_channel; i++) {
        if ((i != index) && get_link_distance(index, i) != inf_distance) {
            total_select_count++;
        }
    }
    select_t* select_list = malloc(sizeof(select_t) * total_select_count);
    assert(select_list != NULL);
    size_t select_count = 0;
    select_list[select_count].channel = done_channel;
    select_list[select_count].is_send = false;
    select_list[select_count].data = NULL;
    select_count++;
    select_list[select_count].channel = channels[index];
    select_list[select_count].is_send = false;
    select_list[select_count].data = NULL;
    select_count++;
    for (size_t i = 0; i < num_channel; i++) {
        if ((i != index) && get_link_distance(index, i) != inf_distance) {
            select_list[select_count].channel = channels[i];
            select_list[select_count].is_send = true;
            select_list[select_count].data = curr_state;
            select_count++;
        }
    }
    while (true) {
        enum chan_status status = channel_select(select_count, select_list, &selected_index);
        if (status == SUCCESS) {
            assert(selected_index != 0);
            if (selected_index == 1) {
                if (select_list[selected_index].data) {
                    // update next_state with new data
                    distance_vector_t* neighbor_state = select_list[selected_index].data;
                    distance_t neighbor_dist = get_link_distance(index, neighbor_state->src);
                    assert(neighbor_dist != inf_distance);
                    for (size_t i = 0; i < num_channel; i++) {
                        distance_t new_dist = neighbor_dist + neighbor_state->dist[i];
                        if (new_dist < next_state->dist[i]) {
                            next_state->dist[i] = new_dist;
                            changed = true;
                        }
                    }
                } else {
                    // special message sent to test convergence
                    bool converged = (select_count == 2) && !changed;
                    status = channel_send(completed_channel, converged ? curr_state : NULL, true);
                    assert(status == SUCCESS);
                }
            } else {
                select_count--;
                // swap last element and selected element
                chan_t* temp = select_list[select_count].channel;
                select_list[select_count].channel = select_list[selected_index].channel;
                select_list[selected_index].channel = temp;
            }
            // check if we've sent to everyone
            if (select_count == 2) {
                // check if we want to reset
                if (changed) {
                    // cycle triple buffer
                    distance_vector_t* temp_state = curr_state;
                    curr_state = next_state;
                    next_state = prev_prev_state;
                    prev_prev_state = prev_state;
                    prev_state = temp_state;
                    next_state->epoch = curr_state->epoch + 1;
                    for (size_t i = 0; i < num_channel; i++) {
                        next_state->dist[i] = curr_state->dist[i];
                    }
                    // reset to broadcast again
                    select_count = total_select_count;
                    for (size_t i = 2; i < select_count; i++) {
                        select_list[i].data = curr_state;
                    }
                    changed = false;
                }
            }
        } else {
            assert(status == CLOSED_ERROR);
            assert(selected_index == 0);
            assert(changed == false);
            break;
        }
    }
    free(select_list);
    free(prev_prev_state);
    free(prev_state);
    free(curr_state);
    free(next_state);
    return NULL;
}

bool check_done()
{
    bool valid = true;
    enum chan_status status;
    distance_vector_t** completed = malloc(sizeof(distance_vector_t*) * num_channel);
    assert(completed != NULL);
    // validate by sending special NULL message to flush channels
    for (size_t i = 0; i < num_channel; i++) {
        status = channel_send(channels[i], NULL, true);
        assert(status == SUCCESS);
    }
    // receive special response
    for (size_t i = 0; i < num_channel; i++) {
        void* data = NULL;
        status = channel_receive(completed_channel, &data, true);
        assert(status == SUCCESS);
        if (data == NULL) {
            valid = false;
        } else {
            distance_vector_t* new_data = (distance_vector_t*)data;
            size_t index = new_data->src;
            completed[index] = new_data;
        }
    }
    if (valid) {
        // ensure epoch hasn't changed since first validation
        for (size_t i = 0; i < num_channel; i++) {
            status = channel_send(channels[i], NULL, true);
            assert(status == SUCCESS);
        }
        // receive special response
        for (size_t i = 0; i < num_channel; i++) {
            void* data = NULL;
            status = channel_receive(completed_channel, &data, true);
            assert(status == SUCCESS);
            if (data == NULL) {
                valid = false;
            } else {
                distance_vector_t* new_data = (distance_vector_t*)data;
                size_t index = new_data->src;
                if (completed[index]->epoch != new_data->epoch) {
                    valid = false;
                }
            }
        }
        if (valid) {
            // check results
            for (size_t src = 0; src < num_channel; src++) {
                for (size_t dst = 0; dst < num_channel; dst++) {
                    assert(completed[src]->dist[dst] == get_solution_distance(src, dst));
                }
            }
        }
    }
    free(completed);
    return valid;
}

void run_stress(size_t main_buffer_size, size_t secondary_buffer_size, const char* filename)
{
    assert(main_buffer_size <= 1); // only support up to a buffer size of 1
    assert(secondary_buffer_size <= 1); // only support up to a buffer size of 1
    int pthread_status;
    enum chan_status status;
    bool initialized = create_topology(filename);
    assert(initialized);
    channels = malloc(sizeof(chan_t*) * num_channel);
    assert(channels != NULL);
    for (size_t i = 0; i < num_channel; i++) {
        channels[i] = channel_create(main_buffer_size);
        assert(channels[i] != NULL);
    }
    done_channel = channel_create(secondary_buffer_size);
    assert(done_channel != NULL);
    completed_channel = channel_create(secondary_buffer_size);
    assert(completed_channel != NULL);

    pthread_t* pid = malloc(sizeof(pthread_t) * num_channel);
    assert(pid != NULL);
    for (size_t i = 0; i < num_channel; i++) {
        pthread_status = pthread_create(&pid[i], NULL, router, (void*)i);
        assert(pthread_status == 0);
    }

    // wait for convergence
    while (!check_done()) {
        usleep(1000);
    }

    // stop threads
    status = channel_close(done_channel);
    assert(status == SUCCESS);
    // join threads
    for (size_t i = 0; i < num_channel; i++) {
        pthread_join(pid[i], NULL);
    }
    // cleanup
    status = channel_destroy(done_channel);
    assert(status == SUCCESS);
    status = channel_close(completed_channel);
    assert(status == SUCCESS);
    status = channel_destroy(completed_channel);
    assert(status == SUCCESS);
    for (size_t i = 0; i < num_channel; i++) {
        status = channel_close(channels[i]);
        assert(status == SUCCESS);
        status = channel_destroy(channels[i]);
        assert(status == SUCCESS);
    }
    free(pid);
    free(channels);
    destroy_topology();
}
