#include <stdio.h>
#include "channel.h"
#include <assert.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/resource.h>
#include <string.h>
#include <stdbool.h>
#include "stress.h"
#include "stress_send_recv.h"

#define mu_str_(text) #text
#define mu_str(text) mu_str_(text)
#define mu_assert(message, test) do { if (!(test)) return "FAILURE: See " __FILE__ " Line " mu_str(__LINE__) ": " message; } while (0)
#define mu_run_test(test) do { char *message = test(); tests_run++; \
                                if (message) return message; } while (0)

typedef struct {
    chan_t *channel;
    void *data;
    enum chan_status out;
    sem_t *done;
} send_args;

typedef struct {
    chan_t *channel;
    void *data;
    enum chan_status out;
    sem_t *done;
} receive_args;

typedef struct {
    select_t *select_list;
    size_t list_size;
    sem_t *done;
    enum chan_status out;
    size_t index;
} select_args;

typedef struct {
    long double data;
    pthread_t pid;
} cpu_args;

int tests_run = 0;
int tests_passed = 0;

int string_equal(const char* str1, const char* str2) {
    if ((str1 == NULL) && (str2 == NULL)) {
        return 1;
    }
    if ((str1 == NULL) || (str2 == NULL)) {
        return 0;
    }
    return (strcmp(str1, str2) == 0);
}

void init_object_for_send_api(send_args* new_args, chan_t* channel, char* message, sem_t* done) {
    new_args->channel = channel;
    new_args->data = message;
    new_args->out = OTHER_ERROR;
    new_args->done = done;
}

void init_object_for_receive_api(receive_args* new_args, chan_t* channel, sem_t* done) {
    new_args->channel = channel;
    new_args->data = NULL;
    new_args->out = OTHER_ERROR;
    new_args->done = done;
}

void init_object_for_select_api(select_args* new_args, select_t *list, size_t list_size, sem_t* done) {
    new_args->select_list = list;
    new_args->list_size = list_size;
    new_args->out = OTHER_ERROR;
    new_args->index = list_size;
    new_args->done = done;
}

void print_test_details(const char* test_name, const char* message) {
    printf("Running test case: %s : %s ...\n", test_name, message);
}

void* helper_send(send_args *myargs) {
    // static int cnt = 0;
    myargs->out = channel_send(myargs->channel, myargs->data, true);
    if (myargs->done) {
        sem_post(myargs->done);
    }
    return NULL;
}

void* helper_receive(receive_args *myargs) {
    myargs->out = channel_receive(myargs->channel, &myargs->data, true);
    if (myargs->done) {
        sem_post(myargs->done);
    }
    return NULL;
}

void* helper_select(select_args *myargs) {
    myargs->out = channel_select(myargs->list_size, myargs->select_list, &myargs->index);
    if (myargs->done) {
        sem_post(myargs->done);
    }
    return NULL; 
}

void* helper_non_blocking_send(send_args *myargs) {
    myargs->out = channel_send(myargs->channel, myargs->data, false);
    if (myargs->done) {
        sem_post(myargs->done);
    }
    return NULL;
}

void* helper_non_blocking_receive(receive_args* myargs) {
    myargs->out = channel_receive(myargs->channel, &myargs->data, false);
    if (myargs->done) {
        sem_post(myargs->done);
    }
    return NULL;
}

char* test_initialization() {
    print_test_details(__func__, "Testing the channel intialization");

    /* In this part of code we are creating a channel and checking if its intialization is correct */
    size_t capacity = 10000;
    chan_t* channel = channel_create(capacity);

    mu_assert("test_initialization: Could not create channel\n", channel != NULL);
    mu_assert("test_initialization: Did not create buffer\n", channel->buffer != NULL);
    mu_assert("test_initialization: Buffer size is not as expected\n", buffer_current_size(channel->buffer) ==  0);   
    mu_assert("test_initialization: Buffer capacity is not as expected\n", buffer_capacity(channel->buffer) ==  capacity);
    
    channel_close(channel);
    channel_destroy(channel);

    return NULL;
}

#define NS_PER_SEC 1000000000ull

uint64_t convertSecondsToTime(double sec)
{
    return (uint64_t)(sec * (double)(NS_PER_SEC));
}

double convertTimeToSeconds(uint64_t t)
{
    return (double)(t) / (double)(NS_PER_SEC);
}

uint64_t convertTimespecToTime(struct timespec* t)
{
    return (uint64_t)t->tv_sec * NS_PER_SEC + (uint64_t)t->tv_nsec;
}

void convertTimeToTimespec(uint64_t t, struct timespec* out)
{
    out->tv_sec = (time_t)(t / NS_PER_SEC);
    out->tv_nsec = (long)(t % NS_PER_SEC);
}

uint64_t getTime()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return convertTimespecToTime(&now);
}

void* average_cpu_utilization(cpu_args* myargs) {

    struct rusage usage1;
    struct rusage usage2;

    getrusage(RUSAGE_SELF, &usage1);
    struct timeval start = usage1.ru_utime;
    struct timeval start_s = usage1.ru_stime;
    sleep(20);
    getrusage(RUSAGE_SELF, &usage2);
    struct timeval end = usage2.ru_utime;
    struct timeval end_s = usage2.ru_stime;
    
    long double result = (end.tv_sec - start.tv_sec)*1000000L + end.tv_usec - start.tv_usec + (end_s.tv_sec - start_s.tv_sec)*1000000L + end_s.tv_usec - start_s.tv_usec;
    
    myargs->data = result;
    return NULL;
}

char* test_send_correctness() {
    print_test_details(__func__, "Testing the send correctness");

    /* In this part of code we create a channel with capacity 2 and spawn 3 threads to call send API.
     * Expected response: 2 threads should return and one should be blocked
     */
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);

    pthread_t pid[3];

    sem_t send_done;
    sem_init(&send_done, 0, 0);
    // Send first Message
    send_args new_args;
    init_object_for_send_api(&new_args, channel, "Message1", &send_done);
    pthread_create(&pid[0], NULL, (void *)helper_send, &new_args);

    // wait before checking if its still blocking
    sem_wait(&send_done);

    mu_assert("test_send_correctness: Testing channel size failed" , buffer_current_size(channel->buffer) == 1);
    mu_assert("test_send_correctness: Testing channel value failed", string_equal(peek_buffer(0, channel->buffer), "Message1"));
    mu_assert("test_send_correctness: Testing channel return failed", new_args.out == SUCCESS);

    // Send second message
    send_args new_args_1;
    init_object_for_send_api(&new_args_1, channel, "Message2", &send_done);
    pthread_create(&pid[1], NULL, (void *)helper_send, &new_args_1);

    // wait before checking if its still blocking
    sem_wait(&send_done);

    mu_assert("test_send_correctness: Testing buffer size failed" , buffer_current_size(channel->buffer) == 2);
    mu_assert("test_send_correctness: Testing channel values failed", string_equal(peek_buffer(0, channel->buffer), "Message1"));
    mu_assert("test_send_correctness: Testing channel values failed", string_equal(peek_buffer(1, channel->buffer), "Message2"));
    mu_assert("test_send_correctness: Testing channel return failed", new_args_1.out == SUCCESS);

    // Send third thread
    send_args new_args_2;
    init_object_for_send_api(&new_args_2, channel, "Message3", &send_done);
    pthread_create(&pid[2], NULL, (void *)helper_send, &new_args_2);

    usleep(10000);

    mu_assert("test_send_correctness: Testing buffer size failed" , buffer_current_size(channel->buffer) == 2);
    mu_assert("test_send_correctness: Testing channel values failed", string_equal(peek_buffer(0, channel->buffer), "Message1"));
    mu_assert("test_send_correctness: Testing channel values failed", string_equal(peek_buffer(1, channel->buffer), "Message2"));
    mu_assert("test_send_correctness: Testing channel values failed", new_args_2.out == OTHER_ERROR);

    // Receiving from the channel to unblock
    void* out = NULL;
    channel_receive(channel, &out, true);

    for (size_t i = 0; i < 3; i++) {
        pthread_join(pid[i], NULL);
    }

    // Empty channel again  
    channel_receive(channel, &out, true);
    channel_receive(channel, &out, true);
    
    // Checking for NULL as a acceptable value
    void* data = NULL;
    channel_send(channel, data, true);
    channel_send(channel, data, true);

    mu_assert("test_send_correctness: Testing buffer size failed" , buffer_current_size(channel->buffer) == 2);
    mu_assert("test_send_correctness: Testing null value", peek_buffer(0, channel->buffer) == NULL);
    mu_assert("test_send_correctness: Testing null value", peek_buffer(1, channel->buffer) == NULL);

    // Clearing the memory  
    channel_close(channel);
    channel_destroy(channel);
    sem_destroy(&send_done);

    return NULL;
}

char* test_receive_correctness() {
    print_test_details(__func__, "Testing the receive correctness");
    /* This test creates a channel of size 2 and calls receive thrice. The expectation 
    * is that the last one is blocked.  
    */
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);

    size_t RECEIVER_THREAD = 2; 
    receive_args data_rec[RECEIVER_THREAD];
    // Filling channel with messages
    channel_send(channel, "Message4", true);
    channel_send(channel, "Message5", true);

    // Checking if the basic receives works
    void* out = NULL;
    channel_receive(channel, &out, true);

    mu_assert("test_receive_correctness: Testing buffer size failed 1\n" , buffer_current_size(channel->buffer) == 1);
    mu_assert("test_receive_correctness: Testing channel values failed 1\n", string_equal(out, "Message4"));


    void* out1 = NULL;
    channel_receive(channel, &out1, true);

    mu_assert("test_receive_correctness: Testing buffer size failed 2\n" , buffer_current_size(channel->buffer) == 0);
    mu_assert("test_receive_correctness: Testing channel values failed 2\n", string_equal(out1, "Message5"));

    pthread_t pid[RECEIVER_THREAD];
    sem_t done;
    sem_init(&done, 0, 0);

    // Start two threads with receives, then send to channel one by one.
    for (size_t i = 0; i < RECEIVER_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, &done);
        pthread_create(&pid[i], NULL, (void *)helper_receive, &data_rec[i]);

    }
    // Sleep is to wait before checking if receive calls are still blocked.
    usleep(10000);

    mu_assert("test_receive_correctness: Testing channel size failed", buffer_current_size(channel->buffer) == 0);
    mu_assert("test_receive_correctness: Testing channel return failed", data_rec[0].out == OTHER_ERROR);
    mu_assert("test_receive_correctness: Testing channel return failed" , data_rec[1].out == OTHER_ERROR);

    channel_send(channel, "Message1", true);
    sem_wait(&done);

    mu_assert("test_receive_correctness: Testing channel return failed" , (data_rec[0].out == SUCCESS || data_rec[1].out == SUCCESS ));
    mu_assert("test_receive_correctness: Testing channel return failed" , !(data_rec[0].out == SUCCESS && data_rec[1].out == SUCCESS ));
    mu_assert("test_receive_correctness: Testing channel return failed" , string_equal(data_rec[0].data, "Message1") || string_equal(data_rec[1].data, "Message1")); 

    channel_send(channel, "Message2", true);
    sem_wait(&done);

    mu_assert("test_receive_correctness: Testing channel return failed" , (data_rec[0].out == SUCCESS && data_rec[1].out == SUCCESS ));
    mu_assert("test_receive_correctness: Testing channel return failed" , string_equal(data_rec[0].data, "Message2") || string_equal(data_rec[1].data, "Message2")); 

    for (size_t i = 0; i < RECEIVER_THREAD; i++) {
        pthread_join(pid[i], NULL);
    }

    // Checking for the NULL values in receive
    void* temp = (void*)0xdeadbeef;
    channel_send(channel, NULL, true);
    channel_receive(channel, &temp, true);

    mu_assert("test_receive_correctness: Testing NULL value from channel" , temp == NULL);

    channel_close(channel);
    channel_destroy(channel);
    sem_destroy(&done);

    return NULL;

}

char* test_overall_send_receive() {
    print_test_details(__func__, "Testing send and receive overall");
    
    /* This test spwans 10 send and receive requests. The expectation is that none of the
     *  calls should get blocked.
    */
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);
    
    size_t RECEIVE_THREAD = 10;
    size_t SEND_THREAD = 10;

    pthread_t rec_pid[RECEIVE_THREAD];
    pthread_t send_pid[SEND_THREAD];

    receive_args data_rec[RECEIVE_THREAD];
    send_args data_send[SEND_THREAD];
    // Spawning multiple send and receives
    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, NULL);
        pthread_create(&rec_pid[i], NULL, (void *)helper_receive, &data_rec[i]);
    }

    for (size_t i = 0; i < SEND_THREAD; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message1", NULL);
        pthread_create(&send_pid[i], NULL, (void *)helper_send, &data_send[i]);  
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        pthread_join(rec_pid[i], NULL);
    }

    for (size_t i = 0; i < SEND_THREAD; i++){
        pthread_join(send_pid[i], NULL);
    }

    for (size_t i = 0; i < SEND_THREAD; i++) {
        mu_assert("test_overall_send_receive: Testing channel send return failed" , data_send[i].out == SUCCESS);
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        mu_assert("test_overall_send_receive: Testing channel receive return value failed" , data_rec[i].out == SUCCESS);
        mu_assert("test_overall_send_receive: Testing channel receive return data failed" , string_equal(data_rec[i].data, "Message1"));
    }

    channel_close(channel);
    channel_destroy(channel);

    return NULL;

}

char* test_non_blocking_send() {
    print_test_details(__func__, "Testing non_blocking send"); 

    /* This test ensures that when you try to send more number of messages 
    * than what the channel can handle, non-blocking send should return with proper status. 
    */
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);

    size_t SEND_THREAD = 10;
    pthread_t send_pid[SEND_THREAD];
    send_args data_send[SEND_THREAD];

    for (size_t i = 0; i < SEND_THREAD; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message1", NULL);
        pthread_create(&send_pid[i], NULL, (void *)helper_non_blocking_send, &data_send[i]); 
    }
    
    for (size_t i = 0; i < SEND_THREAD; i++){
        pthread_join(send_pid[i], NULL);
    }

    size_t send_count = 0;
    for (size_t i = 0; i < SEND_THREAD; i++) {
        if (data_send[i].out == WOULDBLOCK) {
            send_count ++;
        }
    }
    mu_assert("test_non_blocking_send: Testing channel send return value failed" , (send_count == SEND_THREAD - capacity));

    channel_close(channel);
    channel_destroy(channel);

    return NULL;

}

char* test_non_blocking_receive() {
    print_test_details(__func__, "Testing non blocking receive");
    
    /* This test ensures that when you try to receive more number of messages 
    * than what the channel contains, non-blocking receive should return with proper status. 
    */
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);
    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel, "Message", true);
    }

    size_t RECEIVE_THREAD = 10;
    size_t SEND_THREAD = 3;

    pthread_t send_pid[SEND_THREAD];
    pthread_t rec_pid[RECEIVE_THREAD];
    
    receive_args data_rec[RECEIVE_THREAD]; 
    send_args data_send[SEND_THREAD];

    init_object_for_send_api(&data_send[0], channel, "Message1", NULL);
    pthread_create(&send_pid[0], NULL, (void *)helper_send, &data_send[0]);  

    init_object_for_send_api(&data_send[1], channel, "Message2", NULL);
    pthread_create(&send_pid[1], NULL, (void *)helper_send, &data_send[1]);  

    init_object_for_send_api(&data_send[2], channel, "Message3", NULL);
    pthread_create(&send_pid[2], NULL, (void *)helper_send, &data_send[2]);  

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, NULL);
        pthread_create(&rec_pid[i], NULL, (void *)helper_non_blocking_receive, &data_rec[i]);
        // Allow time for sends to take effect
        usleep(100000);
    }

    size_t receive_count = 0;
    int message_received = 0;
    int message1_received = 0;
    int message2_received = 0;
    int message3_received = 0;

    for (size_t i = 0; i < SEND_THREAD; i++) {
        pthread_join(send_pid[i], NULL);
    }   

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        pthread_join(rec_pid[i], NULL);
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        if (data_rec[i].out == WOULDBLOCK) {
            receive_count ++;
        } else {
            if (string_equal(data_rec[i].data, "Message")) {
                message_received++;
            } else if (string_equal(data_rec[i].data, "Message1")) {
                message1_received++;
            } else if (string_equal(data_rec[i].data, "Message2")) {
                message2_received++;
            } else if (string_equal(data_rec[i].data, "Message3")) {
                message3_received++;
            } else {
                mu_assert("test_non_blocking_receive: Received invalid message", 0);
            }
        }
    }

    mu_assert("test_non_blocking_receive: Failed to receive 2 Message messages", message_received == capacity);
    mu_assert("test_non_blocking_receive: Failed to receive Message1", message1_received == 1);
    mu_assert("test_non_blocking_receive: Failed to receive Message2", message2_received == 1);
    mu_assert("test_non_blocking_receive: Failed to receive Message3", message3_received == 1);
    mu_assert("test_non_blocking_receive: Testing channel receive return value failed", (RECEIVE_THREAD - SEND_THREAD - capacity) == receive_count);
    
    channel_close(channel);
    channel_destroy(channel);

    return NULL;
}

char* test_channel_close_with_send() {
    print_test_details(__func__, "Testing channel close API");

    /*This test ensures that all the blocking calls should return with proper status 
     * when the channel gets closed 
    */

    // Testing for the send API 
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);

    size_t SEND_THREAD = 10;
    
    pthread_t send_pid[SEND_THREAD];
    send_args data_send[SEND_THREAD];

    sem_t send_done;
    sem_init(&send_done, 0, 0);
    

    for (size_t i = 0; i < SEND_THREAD; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message1", &send_done);
        pthread_create(&send_pid[i], NULL, (void *)helper_send, &data_send[i]);  
    }
    
    for (size_t i = 0; i < capacity; i++) {
        sem_wait(&send_done);
    }
    mu_assert("test_channel_close_with_send: Testing channel close failed", channel_close(channel) == SUCCESS);

    // XXX: All the threads should return in finite amount of time else it will be in infinite loop. Hence incorrect implementation
    for (size_t i = 0; i < SEND_THREAD; i++) {
        pthread_join(send_pid[i], NULL);    
    }
    size_t count = 0;
    for (size_t i = 0; i < SEND_THREAD; i++) {
        if (data_send[i].out == CLOSED_ERROR) {
            count ++;
        }
    }

    mu_assert("test_channel_close_with_send: Testing channel close failed" , count == SEND_THREAD - capacity);

    // Making a normal send call to check if its closed
    enum chan_status out = channel_send(channel, "Message", true);
    mu_assert("test_channel_close_with_send: Testing channel close failed" , out == CLOSED_ERROR);
    
    
    out = channel_send(channel, "Message", false);
    mu_assert("test_channel_close_with_send: Testing channel close failed" , out == CLOSED_ERROR);

    mu_assert("test_channel_close_with_send: Testing channel double close failed" , channel_close(channel) == CLOSED_ERROR);
    channel_destroy(channel);
    sem_destroy(&send_done);

    return NULL;

}

char* test_channel_close_with_receive() {
    print_test_details(__func__, "Testing channel close API");

    /*This test ensures that all the blocking calls should return with proper status 
     * when the channel gets closed 
     */

    // Testing for the close API 
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);
    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel, "Message", true);
    }

    size_t RECEIVE_THREAD = 10;
    
    receive_args data_rec[RECEIVE_THREAD];
    pthread_t rec_pid[RECEIVE_THREAD];

    sem_t done;
    sem_init(&done, 0, 0);  

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, &done);
        pthread_create(&rec_pid[i], NULL, (void *)helper_receive, &data_rec[i]); 
    }

    // Wait for receive threads to drain the buffer
    for (size_t i = 0; i < capacity; i++) {
        sem_wait(&done);
    }

    // Close channel to stop the rest of the receive threads
    mu_assert("test_channel_close_with_receive: Testing channel close failed", channel_close(channel) == SUCCESS);

    // XXX: All calls should immediately after close else close implementation is incorrect.
    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        pthread_join(rec_pid[i], NULL); 
    }

    size_t count = 0;
    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        if (data_rec[i].out == CLOSED_ERROR) {
            count ++;
        }
    }

    mu_assert("test_channel_close_with_receive: Testing channel close failed" , count == RECEIVE_THREAD - capacity);

    // Making a normal send call to check if its closed
    void *data = NULL;
    enum chan_status out = channel_receive(channel, &data, true);
    mu_assert("test_channel_close_with_receive: Testing channel close failed" , out == CLOSED_ERROR);
    
    out = channel_receive(channel, &data, false);
    mu_assert("test_channel_close_with_receive: Testing channel close failed" , out == CLOSED_ERROR);
    
    mu_assert("test_channel_close_with_receive: Testing channel double close failed" , channel_close(channel) == CLOSED_ERROR);
    channel_destroy(channel);
    sem_destroy(&done);

    return NULL;
}

char* test_multiple_channels() {
    print_test_details(__func__, "Testing creating multiple channels");

    /* This test ensures that multiple channels can work simultaneously
    */ 

    size_t channel1_capacity = 1;
    size_t channel2_capacity = 2;

    chan_t* channel1 = channel_create(channel1_capacity);
    chan_t* channel2 = channel_create(channel2_capacity);

    size_t SEND_THREAD = 4;

    pthread_t send_pid[SEND_THREAD];
    send_args data_send[SEND_THREAD];

    init_object_for_send_api(&data_send[0], channel1, "CHANNEL1_Message1", NULL);
    pthread_create(&send_pid[0], NULL, (void *)helper_send, &data_send[0]);      
    init_object_for_send_api(&data_send[1], channel2, "CHANNEL2_Message1", NULL);
    pthread_create(&send_pid[1], NULL, (void *)helper_send, &data_send[1]); 

    // To ensure first two messages gets delivered first
    pthread_join(send_pid[0], NULL);
    pthread_join(send_pid[1], NULL);

    init_object_for_send_api(&data_send[2], channel1, "CHANNEL1_Message2", NULL);
    pthread_create(&send_pid[2], NULL, (void *)helper_send, &data_send[2]); 
    init_object_for_send_api(&data_send[3], channel2, "CHANNEL2_Message2", NULL);
    pthread_create(&send_pid[3], NULL, (void *)helper_send, &data_send[3]); 

    void *data = NULL;
    enum chan_status out = channel_receive(channel1, &data, true);
    mu_assert("test_multiple_channels: Testing multiple channels1" , string_equal(data, "CHANNEL1_Message1"));
    mu_assert("test_multiple_channels: Testing multiple channels2", out == SUCCESS);

    void *data1 = NULL;
    out = channel_receive(channel2, &data1, true);
    mu_assert("test_multiple_channels: Testing multiple channels3" , string_equal(data1, "CHANNEL2_Message1"));
    mu_assert("test_multiple_channels: Testing multiple channels4", out == SUCCESS);

    void *data2 = NULL;
    out = channel_receive(channel2, &data2, true);
    mu_assert("test_multiple_channels: Testing multiple channels5" , string_equal(data2, "CHANNEL2_Message2"));
    mu_assert("test_multiple_channels: Testing multiple channels6", out == SUCCESS);

    void *data3 = NULL;
    out = channel_receive(channel1, &data3, true);
    mu_assert("test_multiple_channels: Testing multiple channels7" , string_equal(data3, "CHANNEL1_Message2"));
    mu_assert("test_multiple_channels: Testing multiple channels8", out == SUCCESS);

    for (size_t i = 2; i < SEND_THREAD; i++) {
        pthread_join(send_pid[i], NULL);
    }
    
    channel_close(channel1);
    channel_close(channel2);

    channel_destroy(channel1);
    channel_destroy(channel2);

    return NULL;
}

char* test_response_time() {
    print_test_details(__func__, "Testing send/receive response time (takes around 30 seconds)");

    /* This test ensures that all API calls return back in reasonable time frame
    */
    int ITERS = 1000;
    size_t capacity = 2;
    chan_t* channel = channel_create(capacity);

    void *data = NULL;

    pthread_t pid;

    sem_t done;
    sem_init(&done, 0, 0);
    
    uint64_t total_time = 0;
    for (int i = 0; i < ITERS; i++) {
        receive_args data_rec;
        init_object_for_receive_api(&data_rec, channel, &done);
        pthread_create(&pid, NULL, (void *)helper_receive, &data_rec);

        usleep(10000);

        uint64_t t = getTime();
        channel_send(channel, "Message", true);
        sem_wait(&done);
        t = getTime() - t;

        total_time += t;

        pthread_join(pid, NULL);
    }

    double avg_response_time = convertTimeToSeconds(total_time) / (double)ITERS;
    mu_assert("test_response_time: Avg response time for send/receive is higher than 0.0005", avg_response_time < 0.0005);

    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel, "Message", true);
    }

    total_time = 0;
    for (int i = 0; i < ITERS; i++) {
        send_args data_send;
        init_object_for_send_api(&data_send, channel, "Message", &done);
        pthread_create(&pid, NULL, (void *)helper_send, &data_send);      

        usleep(10000);

        uint64_t t = getTime();
        channel_receive(channel, &data, true);
        sem_wait(&done);
        t = getTime() - t;

        total_time += t;
        pthread_join(pid, NULL);
    }

    avg_response_time = convertTimeToSeconds(total_time) / (double)ITERS;
    mu_assert("test_response_time: Avg response time for send/receive is higher than 0.0005", avg_response_time < 0.0005);

    // Free memory
    channel_close(channel);
    channel_destroy(channel);
    sem_destroy(&done);
    return NULL;
}

char* test_select() {
    print_test_details(__func__, "Testing select API");

    /* This test is for select API to work with multiple send and receive requests
    */

    /* This part of code we spawn multiple blocking send calls with select and send one receive */
    size_t CHANNELS = 3;

    pthread_t pid, pid_1;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];
    
    for (size_t i = 0; i < CHANNELS; i++) {
        channel[i] = channel_create(1);
        list[i].is_send = false;
        list[i].channel = channel[i];
    }

    sem_t done;
    sem_init(&done, 0, 0);
    // Testing with empty channels and receive API
    select_args args;
    init_object_for_select_api(&args, list, CHANNELS, &done);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select: It isn't blocked as expected", args.out == OTHER_ERROR);

    channel_send(channel[2], "Message1", true);
    sem_wait(&done);

    // XXX: Code will go in infinite loop here if the select didn't return . 
    pthread_join(pid, NULL);
    mu_assert("test_select: Returned value doesn't match", args.index == 2);

    /* This part of code is to test select with multiple receives */
    for (size_t i = 0; i < CHANNELS; i++) {
        channel_send(channel[i], "Message", true);   
        list[i].is_send = true;
        list[i].data = "Message4";
    }

    select_args args_1;
    init_object_for_select_api(&args_1, list, CHANNELS, &done);
    pthread_create(&pid_1, NULL, (void *)helper_select, &args_1);
    // Before chekcing for block sleep
    usleep(10000);
    mu_assert("test_select: It isn't blocked as expected", args_1.out == OTHER_ERROR);

    void* data;
    channel_receive(channel[0], &data, true);
    sem_wait(&done);

    // XXX: Code will go in infinite loop here if the select didn't return . 
    pthread_join(pid_1, NULL);
    mu_assert("test_select: Returned value doesn't match", args_1.index == 0);

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }
    sem_destroy(&done);
    return NULL;
}

char* test_select_response_time() {
    print_test_details(__func__, "Testing select response time (takes around 30 seconds)");

    /* This test is for select API to work with multiple send and receive requests
    */

    /* This part of code we spawn multiple blocking send calls with select and send one receive */
    int ITERS = 1000;
    size_t CHANNELS = 3;

    pthread_t pid, pid_1;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];
    
    for (size_t i = 0; i < CHANNELS; i++) {
        channel[i] = channel_create(1);
        list[i].is_send = false;
        list[i].channel = channel[i];
    }

    sem_t done;
    sem_init(&done, 0, 0);

    uint64_t total_time = 0;
    for (int i = 0; i < ITERS; i++) {
        // Testing with empty channels and receive API
        select_args args;
        init_object_for_select_api(&args, list, CHANNELS, &done);
        pthread_create(&pid, NULL, (void *)helper_select, &args);

        // To wait sometime before we check select is blocking now
        usleep(10000);

        uint64_t t = getTime();
        channel_send(channel[2], "Message1", true);
        sem_wait(&done);
        t = getTime() - t;

        total_time += t;
        // XXX: Code will go in infinite loop here if the select didn't return . 
        pthread_join(pid, NULL);
    }

    double avg_response_time = convertTimeToSeconds(total_time) / (double)ITERS;
    mu_assert("test_select_response_time: Avg response time for select is higher than 0.0005", avg_response_time < 0.0005);

    /* This part of code is to test select with multiple receives */
    for (size_t i = 0; i < CHANNELS; i++) {
        channel_send(channel[i], "Message", true);   
        list[i].is_send = true;
        list[i].data = "Message4";
    }

    total_time = 0;
    for (int i = 0; i < ITERS; i++) {
        select_args args_1;
        init_object_for_select_api(&args_1, list, CHANNELS, &done);
        pthread_create(&pid_1, NULL, (void *)helper_select, &args_1);

        // Before chekcing for block sleep
        usleep(10000);

        void* data;
        uint64_t t = getTime();
        channel_receive(channel[0], &data, true);
        sem_wait(&done);
        t = getTime() - t;

        total_time += t;
        // XXX: Code will go in infinite loop here if the select didn't return . 
        pthread_join(pid_1, NULL);
    }

    avg_response_time = convertTimeToSeconds(total_time) / (double)ITERS;
    mu_assert("test_select_response_time: Avg response time for select is higher than 0.0005", avg_response_time < 0.0005);

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }
    sem_destroy(&done);
    return NULL;
}

char* test_select_close() {
    print_test_details(__func__, "Testing select with close");

    size_t CHANNELS = 3;

    pthread_t pid;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];

    for (size_t i = 0; i < CHANNELS; i++) {
        channel[i] = channel_create(1);
        list[i].is_send = false;
        list[i].channel = channel[i];
    }

    sem_t done;
    sem_init(&done, 0, 0);
    // Testing with empty channels and receive API
    select_args args;
    init_object_for_select_api(&args, list, CHANNELS, &done);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    usleep(10000);
    mu_assert("test_select_close: Can't close channel", channel_close(channel[0]) == SUCCESS);

    pthread_join(pid, NULL); 
    mu_assert("test_select_close: Channel is closed, it should propogate the same error", args.out == CLOSED_ERROR);
    
    size_t index;
    mu_assert("test_select_close: Select on closed channel should return CLOSED_ERROR", channel_select(CHANNELS, list, &index) == CLOSED_ERROR);

    channel_destroy(channel[0]); // channel[0] was already closed
    for (size_t i = 1; i < CHANNELS; i++) {
        mu_assert("test_select_close: Can't close channel", channel_close(channel[i]) == SUCCESS);
        channel_destroy(channel[i]);
    }
    sem_destroy(&done);
    return NULL;
}

char* test_cpu_utilization_send() {
    print_test_details(__func__, "Testing CPU utilization for send API (takes around 30 seconds)");

    /* This test checks the CPU utilization of all the send APIs.
     */
    // For SEND API
    size_t THREADS = 100;
    size_t capacity = 2;
    chan_t *channel = channel_create(capacity);
    pthread_t cpu_pid, pid[THREADS];

    // Fill buffer
    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel, "Message", true);
    }

    send_args data_send[THREADS];
    for (int i = 0; i < THREADS; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message", NULL);   
        pthread_create(&pid[i], NULL, (void *)helper_send, &data_send[i]);
    }

    sleep(5);

    cpu_args args;
    pthread_create(&cpu_pid, NULL, (void *)average_cpu_utilization, &args);  

    sleep(20);
    pthread_join(cpu_pid, NULL);

    for (int i = 0; i < THREADS; i++) {
        void* data;
        channel_receive(channel, &data, true);
        mu_assert("test_cpu_utilization_send: Invalid message", string_equal(data, "Message"));
    }
    for (int i = 0; i < THREADS; i++) {
        pthread_join(pid[i], NULL);
    }
    mu_assert("test_cpu_utilization_send: CPU Utilization is higher than required", args.data < 50000);

    channel_close(channel);
    channel_destroy(channel);

    return NULL;
}

char* test_cpu_utilization_receive() {
    print_test_details(__func__, "Testing CPU utilization for receive API (takes around 30 seconds)");

    /* This test checks the CPU utilization of all the receive APIs.
     */
    // For RECEIVE API
    size_t THREADS = 100;
    chan_t *channel = channel_create(2);
    pthread_t cpu_pid, pid[THREADS];

    receive_args data_receive[THREADS];
    for (int i = 0; i < THREADS; i++) {
        init_object_for_receive_api(&data_receive[i], channel, NULL);
        pthread_create(&pid[i], NULL, (void *)helper_receive, &data_receive[i]);
    }

    sleep(5);

    cpu_args args;
    pthread_create(&cpu_pid, NULL, (void *)average_cpu_utilization, &args);

    sleep(20);
    pthread_join(cpu_pid, NULL);

    for (int i = 0; i < THREADS; i++) {
        channel_send(channel, "Message", true);
    }
    for (int i = 0; i < THREADS; i++) {
        pthread_join(pid[i], NULL);
        mu_assert("test_cpu_utilization_receive: Invalid message", string_equal(data_receive[i].data, "Message"));
    }
    mu_assert("test_cpu_utilization_receive: CPU Utilization is higher than required", args.data < 50000);
    
    channel_close(channel);
    channel_destroy(channel);

    return NULL;
}

char* test_cpu_utilization_select() {
    print_test_details(__func__, "Testing CPU utilization for select API (takes around 30 seconds)");

    /* This test checks the CPU utilization of all the select APIs.
     */
    // For SELECT API
    size_t THREADS = 100;
    size_t CHANNELS = 3;

    pthread_t pid[THREADS], cpu_pid;
    chan_t* channel[CHANNELS];
    select_t list[THREADS][CHANNELS];

    sem_t done;
    sem_init(&done, 0, 0);

    for (size_t j = 0; j < CHANNELS; j++) {
        channel[j] = channel_create(1);
        for (int i = 0; i < THREADS; i++) {
            list[i][j].is_send = false;
            list[i][j].channel = channel[j];
            list[i][j].data = NULL;
        }
    }

    select_args data_select[THREADS];
    select_args* ptr_data_select[THREADS];
    for (int i = 0; i < THREADS; i++) {
        ptr_data_select[i] = &data_select[i];
        init_object_for_select_api(ptr_data_select[i], list[i], CHANNELS, &done);
        pthread_create(&pid[i], NULL, (void *)helper_select, ptr_data_select[i]);
    }

    sleep(5);

    cpu_args args;
    pthread_create(&cpu_pid, NULL, (void *)average_cpu_utilization, &args);

    sleep(20);
    pthread_join(cpu_pid, NULL);

    for (int i = 0; i < THREADS; i++) {
        channel_send(channel[0], "Message", true);
        sem_wait(&done);
        for (int j = 0; j < THREADS; j++) {
            if (ptr_data_select[j] && ptr_data_select[j]->out == SUCCESS) {
                mu_assert("test_cpu_utilization_select: Invalid select index picked", ptr_data_select[j]->index == 0);
                mu_assert("test_cpu_utilization_select: Invalid message", string_equal(ptr_data_select[j]->select_list[0].data, "Message"));
                pthread_join(pid[j], NULL);
                ptr_data_select[j] = NULL;
            }
        }
    }
    mu_assert("test_cpu_utilization_select: CPU Utilization is higher than required", args.data < 50000);

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }
    sem_destroy(&done);
    return NULL;
}

char* test_free() {
    print_test_details(__func__, "Testing channel destroy");
    /* This test is to check if free memory fails if the channel is not closed */ 
    chan_t *channel = channel_create(2);

    mu_assert("test_free: Doesn't report error if the channel is closed", channel_destroy(channel) == DESTROY_ERROR);

    mu_assert("test_free: Can't close channel", channel_close(channel) == SUCCESS);
    mu_assert("test_free: Can't destroy channel", channel_destroy(channel) == SUCCESS);
    return NULL;
}

char* test_unbuffered() {
    /* This test checks for unbuffered channels (i.e., channel size = 0) */
    print_test_details(__func__, "Testing unbuffered channel");
    
    size_t capacity = 0;
    
    chan_t* channel = channel_create(capacity);

    /* This test spwans 10 send and receive requests. The expectation is that none of the
     *  calls should get blocked.
     */
    
    size_t RECEIVE_THREAD = 10;
    size_t SEND_THREAD = 10;

    pthread_t rec_pid[RECEIVE_THREAD];
    pthread_t send_pid[SEND_THREAD];

    receive_args data_rec[RECEIVE_THREAD];
    send_args data_send[SEND_THREAD];
    // Spawning multiple send and receives
    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, NULL);
        pthread_create(&rec_pid[i], NULL, (void *)helper_receive, &data_rec[i]);
    }

    for (size_t i = 0; i < SEND_THREAD; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message1", NULL);
        pthread_create(&send_pid[i], NULL, (void *)helper_send, &data_send[i]);  
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        pthread_join(rec_pid[i], NULL);
    }

    for (size_t i = 0; i < SEND_THREAD; i++){
        pthread_join(send_pid[i], NULL);
    }

    for (size_t i = 0; i < SEND_THREAD; i++) {
        mu_assert("test_unbuffered: Testing channel send return failed" , data_send[i].out == SUCCESS);
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        mu_assert("test_unbuffered: Testing channel receive return value failed" , data_rec[i].out == SUCCESS);
        mu_assert("test_unbuffered: Testing channel receive return data failed" , string_equal(data_rec[i].data, "Message1"));
    }

    channel_close(channel);
    channel_destroy(channel);

    return NULL;
}


char* test_non_blocking_unbuffered() {
    /* This test checks for unbuffered channels (i.e., channel size = 0) */
    print_test_details(__func__, "Testing unbuffered channel with non_blocking calls");
    
    /* Sanity check for send and recieve. 
     * Send one blocking send followed by un-blocking
     * Send one blocking receive followed by un-blocking
     * Send another non-blocking. It should return immediately . 
     */
    size_t capacity = 0;
    chan_t* channel = channel_create(capacity);
    
    pthread_t s_pid, r_pid;

    send_args send_;
    init_object_for_send_api(&send_, channel, "Message", NULL);
    receive_args rec_;
    init_object_for_receive_api(&rec_, channel, NULL);
    
    pthread_create(&s_pid, NULL, (void *)helper_send, &send_);

    usleep(10000);

    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", channel_send(channel, "Message_", false) == WOULDBLOCK);
    
    void* data = NULL;
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", channel_receive(channel, &data, false) == SUCCESS);
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", string_equal(data, "Message"));
    
    pthread_join(s_pid, NULL);

    pthread_create(&r_pid, NULL, (void *)helper_receive, &rec_);
    void* out;
    
    usleep(10000);

    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", channel_receive(channel, &out, false) == WOULDBLOCK);
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", channel_send(channel, "Message_1", false) == SUCCESS);
    
    pthread_join(r_pid, NULL);
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", rec_.out == SUCCESS);
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer", string_equal(rec_.data, "Message_1"));

    /* This test spwans 10 send and 2 receive requests before it. The expectation is that none of the
     *  calls should get blocked.
     */
    
    size_t RECEIVE_THREAD = 2;
    pthread_t rec_pid[RECEIVE_THREAD];
    receive_args data_rec[RECEIVE_THREAD];
    
    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        init_object_for_receive_api(&data_rec[i], channel, NULL);
        pthread_create(&rec_pid[i], NULL, (void *)helper_receive, &data_rec[i]);
    }

    usleep(10000);

    size_t SEND_THREAD = 10;
    pthread_t send_pid[SEND_THREAD];
    send_args data_send[SEND_THREAD];
    for (size_t i = 0; i < SEND_THREAD; i++) {
        init_object_for_send_api(&data_send[i], channel, "Message1", NULL);
        pthread_create(&send_pid[i], NULL, (void *)helper_non_blocking_send, &data_send[i]);  
    }

    for (size_t i = 0; i < RECEIVE_THREAD; i++) {
        pthread_join(rec_pid[i], NULL);
    }

    for (size_t i = 0; i < SEND_THREAD; i++){
        pthread_join(send_pid[i], NULL);
    }

    int count = 0;
    for (size_t i = 0; i < SEND_THREAD; i++) {
        if (data_send[i].out == WOULDBLOCK) {
            count ++;
        }
    }
    
    mu_assert("test_non_blocking_unbuffered: Testing channel non blocking buffer" , count == 8);

    channel_close(channel);
    channel_destroy(channel);

    return NULL;
    
}

char* test_stress_buffered() {
    print_test_details(__func__, "Stress Testing for buffered channels");
    run_stress(1, 1, "topology.txt");
    run_stress(1, 1, "connected_topology.txt");
    run_stress(1, 1, "random_topology.txt");
    run_stress(1, 1, "random_topology_1.txt");
    run_stress(1, 1, "big_graph.txt");
    return NULL;
}

char* test_stress_unbuffered() {
    print_test_details(__func__, "Stress Testing for unbuffered channels");
    run_stress(0, 0, "topology.txt");
    run_stress(0, 0, "connected_topology.txt");
    run_stress(0, 0, "random_topology.txt");
    run_stress(0, 0, "random_topology_1.txt");
    run_stress(0, 0, "big_graph.txt");
    return NULL;
}

char* test_stress_mixed_buffered_unbuffered() {
    print_test_details(__func__, "Stress Testing for mixing buffered and unbuffered channels");
    run_stress(0, 1, "topology.txt");
    run_stress(0, 1, "connected_topology.txt");
    run_stress(0, 1, "random_topology.txt");
    run_stress(0, 1, "random_topology_1.txt");
    run_stress(0, 1, "big_graph.txt");
    return NULL;
}

char* test_stress_send_recv_buffered() {
    print_test_details(__func__, "Stress Testing send/recv for buffered version (takes around 10 seconds)");
    run_stress_send_recv(1, 4, 0.25, 1000000);
    run_stress_send_recv(1, 8, 0.5, 1000000);
    run_stress_send_recv(1, 16, 0.75, 1000000);
    run_stress_send_recv(4, 4, 0.25, 1000000);
    run_stress_send_recv(4, 8, 0.5, 1000000);
    run_stress_send_recv(4, 16, 0.75, 1000000);
    return NULL;
}

char* test_stress_send_recv_unbuffered() {
    print_test_details(__func__, "Stress Testing send/recv for unbuffered version (takes around 10 seconds)");
    run_stress_send_recv(0, 4, 0.25, 2000000);
    run_stress_send_recv(0, 8, 0.5, 2000000);
    run_stress_send_recv(0, 16, 0.75, 2000000);
    return NULL;
}

char* test_for_basic_global_declaration() {
    print_test_details(__func__, "Testing for global declaration (takes around 20 seconds)");

    /* This test checks the CPU utilization of all the send, receive , select APIs.     
     */
    size_t CHANNELS = 2;
    size_t THREADS = 100;
    pthread_t pid[THREADS], pid_1;
    chan_t* channel[CHANNELS];
    select_t list[THREADS][1];
    select_t list1[1];
    
    // Number of channels
    channel[0] = channel_create(1);
    for (size_t i = 0; i < THREADS; i++) {
        list[i][0].is_send = false;
        list[i][0].channel = channel[0];
    }

    channel[1] = channel_create(1);
    list1[0].is_send = false;
    list1[0].channel = channel[1];

    select_args args[THREADS];
    
    for (size_t i = 0; i < THREADS; i++) {
        init_object_for_select_api(&args[i], list[i], 1, NULL);
        pthread_create(&pid[i], NULL, (void *)helper_select, &args[i]);
    }

    // To wait sometime before we check select is blocking now.
    select_args args1;
    init_object_for_select_api(&args1, list1, 1, NULL);
    pthread_create(&pid_1, NULL, (void *)helper_select, &args1);
    
    usleep(10000);
    mu_assert("test_for_basic_global_declaration: It isn't blocked as expected", args1.out == OTHER_ERROR);

    channel_send(channel[1], "Message1", true);
    
    // XXX: Code will go in infinite loop here if the select didn't return. 
    pthread_join(pid_1, NULL);
    mu_assert("test_for_basic_global_declaration: Returned value doesn't match", args1.index == 0);
    mu_assert("test_for_basic_global_declaration: Returned value doesn't match", string_equal(args1.select_list[0].data, "Message1"));

    struct rusage usage1;
    struct rusage usage2;

    getrusage(RUSAGE_SELF, &usage1);
    struct timeval start = usage1.ru_utime;
    struct timeval start_s = usage1.ru_stime;
    for (size_t i = 0; i < 1000; i++) {
        pthread_create(&pid_1, NULL, (void *)helper_select, &args1);
        channel_send(channel[1], "Message1", true);
        pthread_join(pid_1, NULL);
        usleep(10000);
    }
    getrusage(RUSAGE_SELF, &usage2);
    struct timeval end = usage2.ru_utime;
    struct timeval end_s = usage2.ru_stime;
    
    long double result = (end.tv_sec - start.tv_sec)*1000000L + end.tv_usec - start.tv_usec + (end_s.tv_sec - start_s.tv_sec)*1000000L + end_s.tv_usec - start_s.tv_usec;
    mu_assert("test_for_basic_global_declaration: CPU Utilization is higher than required", result < 2000000);
    
    for (size_t i = 0; i < THREADS; i++) {
        channel_send(channel[0], "Some", true);
    }

    for (size_t i = 0; i < THREADS; i++) {
        pthread_join(pid[i], NULL);
    }

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }
    return NULL;

} 

char* test_for_too_many_wakeups() {
    print_test_details(__func__, "Testing for inefficient design due to too many wakeups (takes around 10 seconds)");

    /* This test checks the CPU utilization of all the send, receive , select APIs.
     */
    size_t THREADS = 100;
    pthread_t pid[THREADS];
    receive_args args[THREADS];
    chan_t* channel = channel_create(1);

    sem_t done;
    sem_init(&done, 0, 0);

    for (size_t i = 0; i < THREADS; i++) {
        init_object_for_receive_api(&args[i], channel, &done);
        pthread_create(&pid[i], NULL, (void *)helper_receive, &args[i]);
    }

    sleep(2);

    struct rusage usage1;
    getrusage(RUSAGE_SELF, &usage1);
    struct timeval start = usage1.ru_utime;
    struct timeval start_s = usage1.ru_stime;

    for (size_t i = 0; i < THREADS; i++) {
        channel_send(channel, "Message", true);
        sem_wait(&done);
        usleep(10000);
    }

    struct rusage usage2;
    getrusage(RUSAGE_SELF, &usage2);
    struct timeval end = usage2.ru_utime;
    struct timeval end_s = usage2.ru_stime;

    long double result = (end.tv_sec - start.tv_sec)*1000000L + end.tv_usec - start.tv_usec + (end_s.tv_sec - start_s.tv_sec)*1000000L + end_s.tv_usec - start_s.tv_usec;
    mu_assert("test_for_too_many_wakeups: CPU Utilization is higher than required", result < 200000);

    for (size_t i = 0; i < THREADS; i++) {
        pthread_join(pid[i], NULL);
    }

    channel_close(channel);
    channel_destroy(channel);
    sem_destroy(&done);
    return NULL;
}

char* test_select_and_non_blocking_send(size_t capacity) {
    size_t CHANNELS = 1;

    pthread_t pid;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];
    
    for (size_t i = 0; i < CHANNELS; i++) {
        channel[i] = channel_create(capacity);
        list[i].is_send = false;
        list[i].channel = channel[i];
    }

    // Testing with empty channels and receive API
    select_args args;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);

    // To wait sometime before we check select is blocking now
    usleep(10000);

    mu_assert("test_select_and_non_blocking_send: It isn't blocked as expected", args.out == OTHER_ERROR);

    mu_assert("test_select_and_non_blocking_send: Non-blocking send failed", channel_send(channel[0], "Message", false) == SUCCESS);
    pthread_join(pid, NULL);    
    
    mu_assert("test_select_and_non_blocking_send: Select failed", args.out == SUCCESS);
    mu_assert("test_select_and_non_blocking_send: Received wrong index", args.index == 0);
    mu_assert("test_select_and_non_blocking_send: Received wrong message", string_equal(args.select_list[0].data, "Message"));

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }

    return NULL;
}

char* test_select_and_non_blocking_receive(size_t capacity) {

    size_t CHANNELS = 1;

    pthread_t pid;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];
 
    for (size_t i = 0; i < CHANNELS; i++) {
        channel[i] = channel_create(capacity);
        list[i].is_send = true;
        list[i].channel = channel[i];
        list[i].data = "Message";
    }

    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel[0], "Message", true);
    }

    // Testing with empty channels and receive API
    select_args args;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);

    // To wait sometime before we check select is blocking now
    usleep(10000);

    mu_assert("test_select_and_non_blocking_receive: It isn't blocked as expected", args.out == OTHER_ERROR);
    
    void *data;
    mu_assert("test_select_and_non_blocking_receive: Non-blocking receive failed", channel_receive(channel[0], &data, false) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_and_non_blocking_receive: Select failed", args.out == SUCCESS);
    mu_assert("test_select_and_non_blocking_receive: Received wrong index", args.index == 0);
    mu_assert("test_select_and_non_blocking_receive: Received wrong message", string_equal(data, "Message"));

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }

    return NULL;
}

char* test_select_and_non_blocking_receive_buffered() {
    print_test_details(__func__, "Testing select and non-blocking receive : buffered");
    return test_select_and_non_blocking_receive(1);
}

char* test_select_and_non_blocking_receive_unbuffered() {
    print_test_details(__func__, "Testing select and non-blocking receive : unbuffered");
    return test_select_and_non_blocking_receive(0);
}

char* test_select_and_non_blocking_send_buffered() {
    print_test_details(__func__, "Testing select and non-blocking send : buffered");
    return test_select_and_non_blocking_send(1);
}

char* test_select_and_non_blocking_send_unbuffered() {
    print_test_details(__func__, "Testing select and non-blocking send : unbuffered");
    return test_select_and_non_blocking_send(0);
}

char* test_select_with_select(size_t capacity) {

    /* Two selects with different operations on same channel */
    size_t SELECT = 2;
    size_t CHANNELS = 1;
    
    pthread_t pid, pid_1;
    chan_t* channel[1];
    select_t list[SELECT][CHANNELS];
 
    channel[0] = channel_create(capacity);
    list[0][0].is_send = true;
    list[0][0].channel = channel[0];
    list[0][0].data = "Message";

    list[1][0].is_send = false;
    list[1][0].channel = channel[0];

    select_args args;
    init_object_for_select_api(&args, list[1], CHANNELS, NULL);

    pthread_create(&pid, NULL, (void *)helper_select, &args);

    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_with_select: It isn't blocked as expected", args.out == OTHER_ERROR);

    select_args args1;
    init_object_for_select_api(&args1, list[0], CHANNELS, NULL);
    pthread_create(&pid_1, NULL, (void *)helper_select, &args1);


    pthread_join(pid_1, NULL);

    pthread_join(pid, NULL);


    mu_assert("test_select_with_select: Select failed", args.out == SUCCESS);
    mu_assert("test_select_with_select: Select failed", args1.out == SUCCESS);
    mu_assert("test_select_with_select: Received wrong index", args.index == 0);
    mu_assert("test_select_with_select: Received wrong index", args1.index == 0);

    mu_assert("test_select_with_select: wrong message recieved", string_equal(args1.select_list[0].data, "Message"));

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }

    return NULL;
}

char* test_select_with_select_buffered () {
    print_test_details(__func__, "Testing select with select : buffered");
    return test_select_with_select(1);
}

char* test_select_with_select_unbuffered () {
    print_test_details(__func__, "Testing select with select : unbuffered");
    return test_select_with_select(0);
}

char* test_select_with_same_channel (size_t capacity) {

    /* Testing with 3 selects receive on same two channels. Only two should be able to process send */
    size_t SELECT = 3;
    size_t CHANNELS = 2;

    pthread_t pid[SELECT];
    chan_t* channel[CHANNELS];
    select_t list[SELECT][CHANNELS];

    for (size_t j = 0; j < CHANNELS; j++) {
        channel[j] = channel_create(capacity);
        for (size_t i = 0; i < SELECT; i++) {
            list[i][j].is_send = false;
            list[i][j].channel = channel[j];
        }
    }

    sem_t done;
    sem_init(&done, 0, 0);

    select_args args[SELECT];
    for (size_t i = 0; i < SELECT; i++) {
        init_object_for_select_api(&args[i], list[i], CHANNELS, &done);
        pthread_create(&pid[i], NULL, (void *)helper_select, &args[i]);
    }

    for (size_t j = 0; j < CHANNELS; j++) {
        channel_send(channel[j], "Message", true);
    }

    for (size_t j = 0; j < CHANNELS; j++) {
        sem_wait(&done);
    }

    size_t success_count = 0;
    for (size_t i = 0; i < SELECT; i++) {
        if (args[i].out == SUCCESS) {
            success_count++;
            mu_assert("test_select_with_same_channel: Wrong message", string_equal(args[i].select_list[args[i].index].data, "Message"));
        }
    }
    mu_assert("test_select_with_same_channel: Only two channels should receive", success_count == CHANNELS);

    for (size_t i = 0; i < (SELECT - CHANNELS); i++) {
        channel_send(channel[0], "Message2", true);
        sem_wait(&done);
    }
    success_count = 0;
    size_t success_count2 = 0;
    for (size_t i = 0; i < SELECT; i++) {
        if (args[i].out == SUCCESS) {
            if (string_equal(args[i].select_list[args[i].index].data, "Message")) {
                success_count++;
            } else if (string_equal(args[i].select_list[args[i].index].data, "Message2")) {
                success_count2++;
            } else {
                mu_assert("test_select_with_same_channel: Wrong message", false);
            }
        }
    }
    mu_assert("test_select_with_same_channel: Only original two channels should receive", success_count == CHANNELS);
    mu_assert("test_select_with_same_channel: All other channels should receive second message", success_count2 == (SELECT - CHANNELS));

    for (size_t i = 0; i < SELECT; i++) {
        pthread_join(pid[i], NULL);
    }

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }
    sem_destroy(&done);
    return NULL;
}

char* test_select_with_same_channel_buffered() {
    print_test_details(__func__, "Testing select with same channel: buffered");
    return test_select_with_same_channel(1);
}

char* test_select_with_same_channel_unbuffered() {
    print_test_details(__func__, "Testing select with same channel : unbuffered");
    return test_select_with_same_channel(0);
}

char* test_select_with_send_receive_on_same_channel(size_t capacity) {

    pthread_t pid[2];
    chan_t* channel = channel_create(capacity);
    select_t list[2][2];
    list[0][0].is_send = false;
    list[0][0].channel = channel;
    list[0][0].data = (void*)0xdeadbeef;
    list[0][1].is_send = true;
    list[0][1].channel = channel;
    list[0][1].data = "Message1";
    
    list[1][0].is_send = false;
    list[1][0].channel = channel;
    list[1][0].data = (void*)0xdeadbeef;
    list[1][1].is_send = true;
    list[1][1].channel = channel;
    list[1][1].data = "Message2";

    select_args args[2];
    init_object_for_select_api(&args[0], list[0], 2, NULL);
    pthread_create(&pid[0], NULL, (void *)helper_select, &args[0]);
    init_object_for_select_api(&args[1], list[1], 2, NULL);
    pthread_create(&pid[1], NULL, (void *)helper_select, &args[1]);

    pthread_join(pid[0], NULL);
    pthread_join(pid[1], NULL);

    mu_assert("test_select_with_send_receive_on_same_channel: Failed select", args[0].out == SUCCESS);
    mu_assert("test_select_with_send_receive_on_same_channel: Failed select", args[1].out == SUCCESS);
    if (args[0].index == 0) {
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid index", args[0].index == 0);
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid index", args[1].index == 1);
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid message", string_equal(args[0].select_list[0].data, "Message2"));
        mu_assert("test_select_with_send_receive_on_same_channel: Overwrote data", args[1].select_list[0].data == (void*)0xdeadbeef);
    } else {
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid index", args[0].index == 1);
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid index", args[1].index == 0);
        mu_assert("test_select_with_send_receive_on_same_channel: Invalid message", string_equal(args[1].select_list[0].data, "Message1"));
        mu_assert("test_select_with_send_receive_on_same_channel: Overwrote data", args[0].select_list[0].data == (void*)0xdeadbeef);
    }
    
    channel_close(channel);
    channel_destroy(channel);
    return NULL;
}

char* test_select_with_send_receive_on_same_channel_buffered() {
    print_test_details(__func__, "Testing select with send/recv on same channel: buffered");
    return test_select_with_send_receive_on_same_channel(1);
}

char* test_select_with_send_receive_on_same_channel_unbuffered() {
    print_test_details(__func__, "Testing select with send/recv on same channel: unbuffered");
    return test_select_with_send_receive_on_same_channel(0);
}

char* test_select_with_duplicate_channel(size_t capacity) {

    // test duplicate receive
    pthread_t pid;
    chan_t* channel = channel_create(capacity);
    select_t list[2];
    list[0].is_send = false;
    list[0].channel = channel;
    list[0].data = (void*)0xdeadbeef;
    list[1].is_send = false;
    list[1].channel = channel;
    list[1].data = (void*)0xdeadbeef;

    select_args args;
    init_object_for_select_api(&args, list, 2, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);

    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_with_duplicate_channel: Send failed", channel_send(channel, "Message", true) == SUCCESS);
    pthread_join(pid, NULL);

    mu_assert("test_select_with_duplicate_channel: Failed select", args.out == SUCCESS);
    mu_assert("test_select_with_duplicate_channel: Invalid index", args.index == 0);
    mu_assert("test_select_with_duplicate_channel: Invalid message", string_equal(args.select_list[0].data, "Message"));
    
    // test duplicate send
    for (size_t i = 0; i < capacity; i++) {
        channel_send(channel, "Message", true);
    }
    list[0].is_send = true;
    list[0].channel = channel;
    list[0].data = "Message1";
    list[1].is_send = true;
    list[1].channel = channel;
    list[1].data = "Message2";

    init_object_for_select_api(&args, list, 2, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);

    // To wait sometime before we check select is blocking now
    usleep(10000);
    void* data = NULL;
    mu_assert("test_select_with_duplicate_channel: Receive failed", channel_receive(channel, &data, true) == SUCCESS);
    pthread_join(pid, NULL);

    mu_assert("test_select_with_duplicate_channel: Failed select", args.out == SUCCESS);
    mu_assert("test_select_with_duplicate_channel: Invalid index", args.index == 0);
    for (size_t i = 0; i < capacity; i++) {
        mu_assert("test_select_with_duplicate_channel: Invalid message", string_equal(data, "Message"));
        mu_assert("test_select_with_duplicate_channel: Receive failed", channel_receive(channel, &data, true) == SUCCESS);
    }
    mu_assert("test_select_with_duplicate_channel: Invalid message", string_equal(data, "Message1"));
    
    channel_close(channel);
    channel_destroy(channel);
    return NULL;
}

char* test_select_with_duplicate_channel_buffered() {
    print_test_details(__func__, "Testing select with duplicate operations on same channel: buffered");
    return test_select_with_duplicate_channel(1);
}

char* test_select_with_duplicate_channel_unbuffered() {
    print_test_details(__func__, "Testing select with duplicate operations on same channel: unbuffered");
    return test_select_with_duplicate_channel(0);
}

char* test_select_mixed_buffered_unbuffered() {
    print_test_details(__func__, "Testing select with a mixture of buffered and unbuffered channels");
    size_t CHANNELS = 4;

    pthread_t pid;
    chan_t* channel[CHANNELS];
    select_t list[CHANNELS];
    select_t list_1[1];

    select_args args;
    void* data;
    size_t index;

    channel[0] = channel_create(0);
    list[0].is_send = false;
    list[0].channel = channel[0];
    channel[1] = channel_create(1);
    list[1].is_send = false;
    list[1].channel = channel[1];
    channel[2] = channel_create(0);
    list[2].is_send = true;
    list[2].data = "Message2";
    list[2].channel = channel[2];
    channel[3] = channel_create(1);
    list[3].is_send = true;
    list[3].data = "Message3";
    list[3].channel = channel[3];

    // fill buffered send buffer
    channel_send(channel[3], "Message1", true);

    // test receive on unbuffered channel
    data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Receive failed", channel_receive(channel[2], &data, true) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 2);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(data, "Message2"));

    // test non blocking receive on unbuffered channel
    data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_mixed_buffered_unbuffered: It isn't blocked as expected", args.out == OTHER_ERROR);
    mu_assert("test_select_mixed_buffered_unbuffered: Non-blocking receive failed", channel_receive(channel[2], &data, false) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 2);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(data, "Message2"));

    // test select receive on unbuffered channel
    list_1[0].is_send = false;
    list_1[0].channel = channel[2];
    list_1[0].data = NULL;
    index = 1;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", channel_select(1, list_1, &index) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 2);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(list_1[0].data, "Message2"));

    // test receive on buffered channel
    data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Receive failed", channel_receive(channel[3], &data, true) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 3);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(data, "Message1"));

    // test non blocking receive on buffered channel
    data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_mixed_buffered_unbuffered: It isn't blocked as expected", args.out == OTHER_ERROR);
    mu_assert("test_select_mixed_buffered_unbuffered: Non-blocking receive failed", channel_receive(channel[3], &data, false) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 3);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(data, "Message3"));

    // test select receive on buffered channel
    list_1[0].is_send = false;
    list_1[0].channel = channel[3];
    list_1[0].data = NULL;
    index = 1;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", channel_select(1, list_1, &index) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 3);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(list_1[0].data, "Message3"));

    // test send on unbuffered channel
    args.select_list[0].data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Send failed", channel_send(channel[0], "Message4", true) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[0].data, "Message4"));

    // test non blocking send on unbuffered channel
    args.select_list[0].data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_mixed_buffered_unbuffered: It isn't blocked as expected", args.out == OTHER_ERROR);
    mu_assert("test_select_mixed_buffered_unbuffered: Non-blocking send failed", channel_send(channel[0], "Message5", false) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[0].data, "Message5"));

    // test select send on unbuffered channel
    list_1[0].is_send = true;
    list_1[0].channel = channel[0];
    list_1[0].data = "Message6";
    index = 1;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", channel_select(1, list_1, &index) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[0].data, "Message6"));

    // test send on buffered channel
    args.select_list[1].data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Send failed", channel_send(channel[1], "Message7", true) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 1);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[1].data, "Message7"));

    // test non blocking send on buffered channel
    args.select_list[1].data = NULL;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    // To wait sometime before we check select is blocking now
    usleep(10000);
    mu_assert("test_select_mixed_buffered_unbuffered: It isn't blocked as expected", args.out == OTHER_ERROR);
    mu_assert("test_select_mixed_buffered_unbuffered: Non-blocking send failed", channel_send(channel[1], "Message8", false) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 1);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[1].data, "Message8"));

    // test select send on buffered channel
    list_1[0].is_send = true;
    list_1[0].channel = channel[1];
    list_1[0].data = "Message9";
    index = 1;
    init_object_for_select_api(&args, list, CHANNELS, NULL);
    pthread_create(&pid, NULL, (void *)helper_select, &args);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", channel_select(1, list_1, &index) == SUCCESS);
    pthread_join(pid, NULL);
    mu_assert("test_select_mixed_buffered_unbuffered: Select failed", args.out == SUCCESS);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", args.index == 1);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong index", index == 0);
    mu_assert("test_select_mixed_buffered_unbuffered: Received wrong message", string_equal(args.select_list[1].data, "Message9"));

    for (size_t i = 0; i < CHANNELS; i++) {
        channel_close(channel[i]);
        channel_destroy(channel[i]);
    }

    return NULL;
}

typedef char* (*test_fn_t)();
typedef struct {
    char* name;
    test_fn_t test;
} test_t;

test_t tests[] = {{"test_initialization", test_initialization},
                  {"test_free", test_free},
                  {"test_send_correctness", test_send_correctness},
                  {"test_receive_correctness", test_receive_correctness},
                  {"test_non_blocking_send", test_non_blocking_send},
                  {"test_non_blocking_receive", test_non_blocking_receive},
                  {"test_multiple_channels", test_multiple_channels},
                  {"test_overall_send_receive", test_overall_send_receive},
                  {"test_stress_send_recv_buffered", test_stress_send_recv_buffered},
                  {"test_response_time", test_response_time},
                  {"test_cpu_utilization_send", test_cpu_utilization_send},
                  {"test_cpu_utilization_receive", test_cpu_utilization_receive},
                  {"test_channel_close_with_send", test_channel_close_with_send},
                  {"test_channel_close_with_receive", test_channel_close_with_receive},
                  {"test_select", test_select},
                  {"test_select_close", test_select_close},
                  {"test_select_and_non_blocking_send_buffered", test_select_and_non_blocking_send_buffered},
                  {"test_select_and_non_blocking_receive_buffered", test_select_and_non_blocking_receive_buffered},
                  {"test_select_with_select_buffered", test_select_with_select_buffered},
                  {"test_select_with_same_channel_buffered", test_select_with_same_channel_buffered},
                  {"test_select_with_send_receive_on_same_channel_buffered", test_select_with_send_receive_on_same_channel_buffered},
                  {"test_select_with_duplicate_channel_buffered", test_select_with_duplicate_channel_buffered},
                  {"test_stress_buffered", test_stress_buffered},
                  {"test_select_response_time", test_select_response_time},
                  {"test_cpu_utilization_select", test_cpu_utilization_select},
                  {"test_for_basic_global_declaration", test_for_basic_global_declaration},
                  {"test_for_too_many_wakeups", test_for_too_many_wakeups},
                  {"test_unbuffered", test_unbuffered},
                  {"test_non_blocking_unbuffered", test_non_blocking_unbuffered},
                  {"test_stress_send_recv_unbuffered", test_stress_send_recv_unbuffered},
                  {"test_select_and_non_blocking_send_unbuffered", test_select_and_non_blocking_send_unbuffered},
                  {"test_select_and_non_blocking_receive_unbuffered", test_select_and_non_blocking_receive_unbuffered},
                  {"test_select_with_select_unbuffered", test_select_with_select_unbuffered},
                  {"test_select_with_same_channel_unbuffered", test_select_with_same_channel_unbuffered},
                  {"test_select_with_send_receive_on_same_channel_unbuffered", test_select_with_send_receive_on_same_channel_unbuffered},
                  {"test_select_with_duplicate_channel_unbuffered", test_select_with_duplicate_channel_unbuffered},
                  {"test_select_mixed_buffered_unbuffered", test_select_mixed_buffered_unbuffered},
                  {"test_stress_unbuffered", test_stress_unbuffered},
                  {"test_stress_mixed_buffered_unbuffered", test_stress_mixed_buffered_unbuffered},
};

size_t num_tests = sizeof(tests)/sizeof(tests[0]);

char* single_test(test_fn_t test, size_t iters) {
    for (size_t i = 0; i < iters; i++) {
        mu_run_test(test);
    }
    return NULL;
}

char* all_tests(size_t iters) {
    for (size_t i = 0; i < num_tests; i++) {
        char* result = single_test(tests[i].test, iters);
        if (result != NULL) {
            return result;
        }
    }
    return NULL;
}

int main(int argc, char** argv) {
    char* result = NULL;
    size_t iters = 1;
    if (argc == 1) {
        result = all_tests(iters);
        if (result != NULL) {
            printf("%s\n", result);
        } else {
            printf("ALL TESTS PASSED\n");
        }

        printf("Tests run: %d\n", tests_run);
 
        return result != NULL;
    } else if (argc == 3) {
        iters = (size_t)atoi(argv[2]);
    } else if (argc > 3) {
        printf("Wrong number of arguments, only one test is accepted at time");
    }

    result = "Did not find test";

    for (size_t i = 0; i < num_tests; i++) {
        if (string_equal(argv[1], tests[i].name)) {
            result = single_test(tests[i].test, iters);
            break;
        }
    }
    if (result) {
        printf("%s\n", result);
    }
    else {
        printf("ALL TESTS PASSED\n");
    }

    printf("Tests run: %d\n", tests_run);

    return result != NULL;
}
