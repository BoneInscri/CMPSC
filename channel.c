#include "channel.h"

// in order to finish broadcast, I create my semaphore to get a count!
static void my_sema_init(my_sema *s, int value)
{
    sem_init(&s->sema, 0, (unsigned int)value);
    s->count = value;
    pthread_mutex_init(&s->count_mutex, NULL);
}

// static void my_sema_wait(my_sema *s)
// {
//     Mysema_Lock(s);
//     s->count--;
//     // printf("wait , count : %d -> %d\n", s->count + 1, s->count);
//     Mysema_Unlock(s);

//     sem_wait(&s->sema);
// }

static void my_sema_post(my_sema *s)
{
    Mysema_Lock(s);
    s->count++;
    // printf("post , count : %d -> %d\n", s->count - 1, s->count);
    sem_post(&s->sema);
    Mysema_Unlock(s);
}

static void my_sema_destory(my_sema *s, int free_flag)
{
    sem_destroy(&s->sema);
    pthread_mutex_destroy(&s->count_mutex);
    if (free_flag)
    {
        free(s);
    }
}

// type : 0 , send
// type : 1 , recv
// type : 2 , select_send
// type : 3 , select_recv

static my_sema *channel_mysema_init(chan_t *channel, int type)
{
    assert(type == SEND || type == RECV || type == SELECT_);
    if (channel == NULL)
    {
        assert(type == SELECT_);
    }

    // malloc a new sema
    my_sema *sem_new = (my_sema *)malloc(sizeof(my_sema));
    assert(sem_new);

    // init
    size_t value = (type == SEND ? buffer_capacity(channel->buffer) : 0); // RECV or SELEC : 0

    my_sema_init(sem_new, (int)value);

    return sem_new;
}

// sema wait
static void channel_sema_wait(chan_t *channel, int type)
{
    assert(channel);
    assert(type == SEND || type == RECV);

    // get sema
    my_sema *sema_cur = channel->sema[type];

    // wait
    // my_sema_wait(sema_cur);
    Mysema_Lock(sema_cur);
    sema_cur->count--;
    // printf("wait , count : %d -> %d\n", s->count + 1, s->count);
    Mysema_Unlock(sema_cur);

    Channel_Unlock(channel);
    sem_wait(&sema_cur->sema);
}

// __attribute__((used))
static int channel_select_signal(chan_t *channel, int type)
{
    assert(channel);
    assert(type == SELECT_SEND || type == SELECT_RECV);

    list_t *list_head = channel->select_sema_list[type];

    list_node_t *node_tmp = list_begin(list_head);

    while (node_tmp != &list_head->head)
    {
        my_sema *sema_cur = (my_sema *)(node_tmp->data);

        Mysema_Lock(sema_cur);
        assert(sema_cur->count <= 0);
        // printf("iter, channel %p, sema_cur %p, select signal yes, count : %d\n", channel, sema_cur, sema_cur->count);

        // int value;
        // sem_getvalue(&sema_cur->sema, &value);
        if (sema_cur->count != 0)
        {
            Mysema_Unlock(sema_cur);

            my_sema_post(sema_cur);
            list_remove(list_head, node_tmp, 0); // alloc = 0, not free data (must!!!)
            // printf("channel %p, sema_first %p, select signal yes, count : %d, value : %d\n", channel, sema_cur, sema_cur->count, value);
            return 1;
        }
        else
        {
            Mysema_Unlock(sema_cur);
            node_tmp = list_next(node_tmp);

            // printf("select signal next, count : %d, value : %d\n", sema_cur->count, value);
        }
    }
    // printf("select signal fail\n");
    return 0;
}

// sema signal
static void channel_sema_signal(chan_t *channel, int type)
{
    assert(channel);
    assert(type == SEND || type == RECV);
    // get sema
    my_sema *sema_cur = channel->sema[type];

    Mysema_Lock(sema_cur);
    if (type == SEND && sema_cur->count == buffer_capacity(channel->buffer))
    {
        // printf("count : %d\n", sema_cur->count);
        Mysema_Unlock(sema_cur);
        if (!Channel_Select_Send_Sema_signal(channel))
        // if (!channel_select_signal(channel, SELECT_SEND))
        {
            channel->select_count[SELECT_SEND]++;
            // printf("select send, %d\n", channel->select_count[SELECT_SEND]);
        }
        return;
    }

    if (type == RECV && sema_cur->count == 0)
    {
        Mysema_Unlock(sema_cur);
        if (!Channel_Select_Recv_Sema_signal(channel))
        // if (!channel_select_signal(channel, SELECT_RECV))
        {
            channel->select_count[SELECT_RECV]++;
            // printf("select receive, %d\n", channel->select_count[SELECT_RECV]);
        }
        return;
    }
    Mysema_Unlock(sema_cur);

    // printf("sema_signal %s\n", type ? "RECV" : "SEND");
    // signal
    my_sema_post(sema_cur);
}

// sema broadcast
static void channel_sema_broadcast(chan_t *channel, int type)
{
    assert(channel);
    my_sema *sema_cur = channel->sema[type];

    Mysema_Lock(sema_cur);
    int wakeup_cnt = sema_cur->count;
    Mysema_Unlock(sema_cur);

    while (wakeup_cnt < 0)
    {
        my_sema_post(sema_cur);
        wakeup_cnt++;
    }
}

// free entries in channel
static void channel_free(chan_t *channel)
{
    assert(channel);

    Channel_Lock(channel);
    // buffer
    buffer_free(channel->buffer);

    // sema[2]
    my_sema_destory(channel->sema[SEND], 1); // free it
    my_sema_destory(channel->sema[RECV], 1); // free it

    // select_sema_list
    list_destroy(channel->select_sema_list[SELECT_RECV], 1, 1); // select recv sema list, alloc = 1, free_head = 1
    list_destroy(channel->select_sema_list[SELECT_SEND], 1, 1); // select send sema list, alloc = 1, free_head = 1

    Channel_Unlock(channel);
    // destory mutex
    pthread_mutex_destroy(&channel->mutex);

    assert(channel);
    // channel itself
    free(channel);
}

// is closed atomic ?
static int channel_is_closed_atomic(chan_t *channel)
{
    Channel_Lock(channel);
    int ret = Channel_is_closed(channel);
    Channel_Unlock(channel);
    return ret;
}

static int channel_select_save_sema(size_t channel_count, select_t *channel_list, my_sema *sema)
{
    select_t sel_cur;
    chan_t *channel;
    for (int idx = 0; idx < channel_count; idx++)
    {
        // iter the channel list
        sel_cur = channel_list[idx];
        channel = sel_cur.channel;
        Channel_Lock(channel);

        // if it has been signaled ?
        int type = sel_cur.is_send;
        // printf("save sema, channel : %p, type : %s, count : %d\n", channel, type?"SEND":"RECV", channel->select_count[type]);
        if ((channel->select_count)[type])
        {
            (channel->select_count)[type]--;
            // printf("%d \n", channel->select_count[type]);
            Channel_Unlock(channel);
            return 0;
        }

        // insert it into the list
        list_t *list_head = channel->select_sema_list[type];
        list_insert(list_head, (void *)sema);

        // printf("save, sema : %p, channel : %p, list_count : %ld\n", sema, channel, list_count(list_head));
        Channel_Unlock(channel);
    }
    return 1;
}

static void channel_select_restore_sema(size_t channel_count, select_t *channel_list, my_sema *sema)
{
    select_t sel_cur;
    chan_t *channel;
    for (int idx = 0; idx < channel_count; idx++)
    {
        // iter the channel list
        sel_cur = channel_list[idx];
        channel = sel_cur.channel;
        Channel_Lock(channel);

        // find the target sema node, delete it from the list and free it!
        int type = sel_cur.is_send;
        list_t *list_head = channel->select_sema_list[type];
        list_node_t *node = list_find(list_head, (void *)sema);

        // assert(node);
        if (node)                            // it has been removed!
            list_remove(list_head, node, 0); // alloc = 0, not free data

        // printf("restore, sema : %p, channel : %p, list_count : %ld\n", sema, channel, list_count(list_head));

        Channel_Unlock(channel);
    }
}

void select_list_handler(void *data)
{
    // sem_t *sem_cur = (sem_t *)data;
    // sem_post(sem_cur);
    my_sema *sem_cur = (my_sema *)data;
    my_sema_post(sem_cur);
}

static void channel_select_broadcast(chan_t *channel, int type)
{
    list_t *list_head = channel->select_sema_list[type];
    list_foreach_safe(list_head, select_list_handler);
}

// lock all mutexs
// static void channel_select_lock(size_t channel_count, select_t *channel_list)
// {
//     select_t *sel_cur;
//     chan_t *channel;
//     for (int idx = 0; idx < channel_count; idx++)
//     {
//         sel_cur = channel_list + idx;
//         channel = sel_cur->channel;
//         Channel_Lock(channel);
//     }
// }

// static void channel_select_unlock(size_t channel_count, select_t *channel_list)
// {
//     select_t *sel_cur;
//     chan_t *channel;
//     for (int idx = 0; idx < channel_count; idx++)
//     {
//         sel_cur = channel_list + idx;
//         channel = sel_cur->channel;
//         Channel_Unlock(channel);
//     }
// }

// Creates a new channel with the provided size and returns it to the caller
// A 0 size indicates an unbuffered channel, whereas a positive size indicates a buffered channel
chan_t *channel_create(size_t size)
{
    chan_t *channel = (chan_t *)malloc(sizeof(chan_t));
    assert(channel != NULL);

    channel->buffer = buffer_create(size);
    channel->closed = 0;
    // mutex -> lock
    pthread_mutex_init(&channel->mutex, NULL);
    // semaphore -> sleep or wakeup
    channel->sema[SEND] = channel_mysema_init(channel, SEND);
    channel->sema[RECV] = channel_mysema_init(channel, RECV);

    // int value;
    // sem_getvalue(channel->sema[SEND], &value);
    // printf("init value : %d\n", value);

    // select sema list
    channel->select_sema_list[SELECT_SEND] = list_create();
    channel->select_sema_list[SELECT_RECV] = list_create();

    // select count
    channel->select_count[SELECT_SEND] = 0;
    channel->select_count[SELECT_RECV] = 0;
    return channel;
}

// Writes data to the given channel
// This can be both a blocking call i.e., the function only returns on a successful completion of send (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is full (blocking = false)
// In case of the blocking call when the channel is full, the function waits till the channel has space to write the new data
// Returns SUCCESS for successfully writing data to the channel,
// WOULDBLOCK if the channel is full and the data was not added to the buffer (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_send(chan_t *channel, void *data, bool blocking)
{
    enum chan_status ret_status = OTHER_ERROR;
    // static int cnt = 0;
try_send:
    Channel_Lock(channel);
    if (Channel_is_closed(channel)) // channel is closed
    {
        ret_status = CLOSED_ERROR;
        goto ret;
    }
    if (!buffer_add(data, channel->buffer)) // send fail, buffer is full
    {

        if (blocking == true) // blocking, just wait
        {
            Channel_Send_Sema_wait(channel);
            goto try_send;
        }
        else
        {
            // non-blocking
            // simply return
            ret_status = WOULDBLOCK;
        }
    }
    else // buffer is not full
    {
        // if (!Channel_Select_Recv_Sema_signal(channel))
        // {
        //     // printf("Miss\n");
        //     Channel_Recv_Sema_signal(channel);
        // }

        // printf("channel : %p, send yes\n", channel);
        Channel_Recv_Sema_signal(channel);
        ret_status = SUCCESS;
        // add data to buffer successfully
    }

ret:
    Channel_Unlock(channel);
    return ret_status;
}

// Reads data from the given channel and stores it in the function’s input parameter, data (Note that it is a double pointer).
// This can be both a blocking call i.e., the function only returns on a successful completion of receive (blocking = true), and
// a non-blocking call i.e., the function simply returns if the channel is empty (blocking = false)
// In case of the blocking call when the channel is empty, the function waits till the channel has some data to read
// Returns SUCCESS for successful retrieval of data,
// WOULDBLOCK if the channel is empty and nothing was stored in data (non-blocking calls only),
// CLOSED_ERROR if the channel is closed, and
// OTHER_ERROR on encountering any other generic error of any sort
enum chan_status channel_receive(chan_t *channel, void **data, bool blocking)
{
    enum chan_status ret_status = OTHER_ERROR;
try_recv:
    Channel_Lock(channel);
    if (Channel_is_closed(channel)) // channel is closed
    {
        ret_status = CLOSED_ERROR;
        goto ret;
    }

    void *data_ret = buffer_remove(channel->buffer);
    if (data_ret == BUFFER_EMPTY) // buffer is empty
    {

        if (blocking == true) // blocking, just wait
        {
            Channel_Recv_Sema_wait(channel);
            goto try_recv;
        }
        else
        {
            // non-blocking
            // simply return
            ret_status = WOULDBLOCK;
        }
    }
    else
    {
        // get data from buffer successfully
        *data = data_ret;
        // if (!Channel_Select_Send_Sema_signal(channel))
        // {
        //     Channel_Send_Sema_signal(channel);
        // }
        // printf("channel : %p, receive yes\n", channel);
        Channel_Send_Sema_signal(channel);
        ret_status = SUCCESS;
    }
ret:
    Channel_Unlock(channel);
    return ret_status;
}

// Closes the channel and informs all the blocking send/receive/select calls to return with CLOSED_ERROR
// Once the channel is closed, send/receive/select operations will cease to function and just return CLOSED_ERROR
// Returns SUCCESS if close is successful,
// CLOSED_ERROR if the channel is already closed, and
// OTHER_ERROR in any other error case
enum chan_status channel_close(chan_t *channel)
{
    enum chan_status ret_status = OTHER_ERROR;

    Channel_Lock(channel);
    if (Channel_is_closed(channel)) // have closed
    {
        ret_status = CLOSED_ERROR;
    }
    else // haven't closed
    {
        Channel_set_closed(channel);                 // channel is closed
        Channel_Send_Sema_broadcast(channel);        // inform all sleeping in the Send list
        Channel_Recv_Sema_broadcast(channel);        // inform all sleeping in the Recv list
        Channel_Select_Send_Sema_broadcast(channel); // inform all sleeping in the Select Send list
        Channel_Select_Recv_Sema_broadcast(channel); // inform all sleeping in the Select Recv list

        ret_status = SUCCESS;
    }

    Channel_Unlock(channel);
    return ret_status;
}

// Frees all the memory allocated to the channel
// The caller is responsible for calling channel_close and waiting for all threads to finish their tasks before calling channel_destroy
// Returns SUCCESS if destroy is successful,
// DESTROY_ERROR if channel_destroy is called on an open channel, and
// OTHER_ERROR in any other error case
enum chan_status channel_destroy(chan_t *channel)
{
    enum chan_status ret_status = OTHER_ERROR;

    if (!channel_is_closed_atomic(channel))
    { // havn't closed
        ret_status = DESTROY_ERROR;
    }
    else
    {
        channel_free(channel);
        ret_status = SUCCESS;
    }
    return ret_status;
}

// Takes an array of channels, channel_list, of type select_t and the array length, channel_count, as inputs
// This API iterates over the provided list and finds the set of possible channels which can be used to invoke the required operation (send or receive) specified in select_t
// If multiple options are available, it selects the first option and performs its corresponding action
// If no channel is available, the call is blocked and waits till it finds a channel which supports its required operation
// Once an operation has been successfully performed, select should set selected_index to the index of the channel that performed the operation and then return SUCCESS
// In the event that a channel is closed or encounters any error, the error should be propagated and returned through select
// Additionally, selected_index is set to the index of the channel that generated the error
enum chan_status channel_select(size_t channel_count, select_t *channel_list, size_t *selected_index)
{
    enum chan_status ret_status = OTHER_ERROR;
    select_t *sel_cur;
    chan_t *channel;
    // channel_select_lock(channel_count, channel_list);
    // printf("=============================\n");
try_select:
    size_t idx = 0;
    for (; idx < channel_count; idx++)
    {
        // iter the channel list
        sel_cur = channel_list + idx;
        channel = sel_cur->channel;
        Channel_Lock(channel);
        if (Channel_is_closed(channel)) // have closed
        {
            Channel_Unlock(channel);
            ret_status = CLOSED_ERROR;
            goto ret;
        }

        if (sel_cur->is_send) // send message
        {
            if (buffer_add(sel_cur->data, channel->buffer)) // buffer is not full
            {
                ret_status = SUCCESS;
                // printf("select %p, send target! idx : %ld\n", channel_list, idx);

                // add a new data successfully
                // if (!Channel_Select_Recv_Sema_signal(channel))
                // {
                //     Channel_Recv_Sema_signal(channel);
                // }
                Channel_Recv_Sema_signal(channel);
                Channel_Unlock(channel);
                goto ret;
            }
        }
        else // receive message
        {
            void *data_ret = buffer_remove(channel->buffer);
            if (data_ret != BUFFER_EMPTY) // buffer is not empty
            {

                ret_status = SUCCESS;
                sel_cur->data = data_ret; // don't forget it !
                // printf("select %p, receive target! idx : %ld\n", channel_list, idx);

                // receive a new data successfully
                // if (!Channel_Select_Send_Sema_signal(channel))
                // {
                //     Channel_Send_Sema_signal(channel);
                // }
                Channel_Send_Sema_signal(channel);
                Channel_Unlock(channel);
                goto ret;
            }
        }
        Channel_Unlock(channel);
    }
ret:
    if (idx < channel_count)
    { // exist a channel which is available
        *selected_index = idx;
        // channel_select_unlock(channel_count, channel_list);
    }
    else
    {
        // printf("select : %p, no target\n", channel_list);
        // no channel is available, it need sleep!
        assert(idx == channel_count);
        // local variable, in fact, this variable is from malloc...
        // sem_t selec_sem;
        // sem_init(&selec_sem, 0, 0);
        my_sema *selec_sem = channel_mysema_init(NULL, SELECT_);
        // printf("save sema : %p\n", selec_sem);
        // my_sema_wait(selec_sem);
        Mysema_Lock(selec_sem);
        selec_sem->count--;
        // printf("wait , count : %d -> %d\n", selec_sem->count + 1, selec_sem->count);
        Mysema_Unlock(selec_sem);
        // channel_select_unlock(channel_count, channel_list); // unlock all mutexs!!!

        int ret = channel_select_save_sema(channel_count, channel_list, selec_sem);
        if (ret == 0)
        {
            // printf("hit\n");
            // printf("select : %p, not sleep\n", channel_list);
            channel_select_restore_sema(channel_count, channel_list, selec_sem);
            my_sema_destory(selec_sem, 1); // from malloc, free it
            goto try_select;
        }

        // printf("select %p, select sleep\n", channel_list);
        sem_wait(&selec_sem->sema);
        // printf("select %p, wakeup\n", channel_list);

        // channel_select_lock(channel_count, channel_list);
        channel_select_restore_sema(channel_count, channel_list, selec_sem);

        // destory sema and free it
        my_sema_destory(selec_sem, 1); // from malloc, free it

        goto try_select;
    }

    return ret_status;
}