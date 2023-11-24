# CMPSC

使用C实现 channel 



目标：

完成channel.c、channel.h、linked_list.h和linked_list.c四个文件的基础功能。

保证channel暴露的接口和list暴露的接口功能的正确性，尤其需要正确使用mutex和semaphore，来防止data race 和 lost wakeup。

由于buffer.c 中的接口不是线程安全的，那么就需要pthread_mutex_lock进行保护。

为了实现阻塞需要使用信号量sem_t，但是Linux下的sem_getvalue不会返回负数（表示阻塞等待的线程数），于是我添加了一个有count的my_sema，方便我使用。

```c
typedef struct 
{
    sem_t sema;
    int count;
    pthread_mutex_t count_mutex;
} my_sema;
```

为了防止丢失唤醒和保证channel 结构体entry的互斥，增加了一些变量：

```c
pthread_mutex_t mutex;
int closed;
my_sema* sema[2]; // sema[0] : recv , sema[1] : send
list_t *select_sema_list[2]; // for select , list[0] : recv_list, list[1] : send_list
int select_count[2]; // count[0] : recv, count[1] : send
```

- mutex是保证channel的互斥访问
- closed表示channel是否已经关闭
- sema 是针对send和receive 的信号量（生产者消费者问题基本写法）
- select_sema_list是针对select的挂起队列（区分select_send和select_receive）
- select_count是一个tricky吧，避免丢失唤醒！

为了让编码更加方便，我在channel.h中填加了一些宏，做到了接口的统一和对称。

从channel.c中的接口就可以看出对于channel来说，无非就是send和receive两种操作，在代码实现上是高度对称的！



step 1：

完成链表的接口。

为了减少不必要的特判，我将list_t结构体的head从指针类型改成了结构体。

```c
typedef struct {
    list_node_t head;// not use pointer; instead a structure
    size_t count;
} list_t;
```

为了让链表实现更加方便和容易，这个链表的写法就是Linux kernel中list.h的写法：一个带头结点的双向循环链表。

实现list_init、list_empty和list_append就好实现剩下的接口。（list_append就是在链表的最后进行填加）

list_begin、list_next、list_data和list_count直接返回结构体的成员就可以了。

list_create、list_remove、list_insert就是基本的链表初始化、链表移除和插入的写法（注意语句的顺序，最好画图理解），不过需要注意是否需要free，否则会内存泄漏。

list_find、list_foreach_safe和list_destory都是需要遍历链表进行操作，需要注意循环结束条件是指针回到链表的头结点。特别需要注意的是list_destory每次都是删除第一个结点！list_foreach_safe就是对链表的每个结点进行一次回调函数（传入一个函数指针，然后通过函数指针进行函数调用），为什么是safe？防止进行操作后对链表进行了修改，从而丢失了下一个结点。





step 2：

完成channel的接口

channel_create和channel_destory需要一起写。

为了防止忘记某个变量的初始化和内存释放，最好就是看着结构体一个entry一个entry的进行初始化和释放！

需要传递到函数外部的指针就用malloc，buffer用buffer_create即可，closed，count初始化为0，list初始化用list_create，信号量用自定义的接口：channel_mysema_init。

mysema写得可能有点tricky，不过思想就是在原先的sema_t基础上加一个通过pthread_mutex保护的count，主要是方便broadcast。

需要注意send的信号量初始化为buffer的capacity，receive和select的需要初始化为0。

channel_destory需要注意先判断一下channel的状态，然后进行channel_free（就是把channel_create中创建的变量逐一释放，比如buffer通过buffer_free释放，pthread_mutex通过pthread_mutex_destory销毁，channel本身是malloc产生的，记得free。sema和list通过my_sema_destory和list_destroy销毁回收内存即可。

说到channel接口得返回类型，看一看到初始化都是为OTHER_ERROR，成功就赋值为SUCCESS，其他异常情况就赋值为CLOSED_ERROR、DESTROY_ERROR，然后直接返回。

然后就是几个主要的接口（static开头的函数都是辅助函数）：

channel_send、channel_receive、channel_select和channel_close。

send和receive的写法是对称的，基本方法都是先获取锁，如果channel已经close了，那么就直接返回。如果buffer添加/移除成功，那么就直接signal一下等待的线程，然后返回。为了避免死锁，一定记住在返回的时候释放锁和在wait前释放锁。而且为了保证原子性，在sem_wait的前一个语句一定是释放channel的锁！如果blocking是false，即不阻塞，那么设置状态为WOULDBLOCK，直接返回即可。

如果你仔细看代码，会发现代码中有一些goto（本质就是一个无条件跳转）的写法，不要感到惊讶（原则上我们是不使用goto语句，防止程序结构的混乱），在Linux内核中goto是十分常见的，主要用于一些异常处理的判断。（有时候goto的使用会极大方便我们的编码）

chanel_select是本次project最大的难点，主要就是如何编码保证一个线程在多个channel上进行wait？

一个比较好的想法是通过一个局部变量的semaphore对一个channel_list进行绑定，每次在进行休眠前将这个变量挂到channel_list的的所有channel中休眠。然后send、receive和select就可以通过channel_sema_signal这个接口进行唤醒。至于这个接口为什么这么写，emmm，有点复杂。。。并发程序是这样的。

if (type == SEND && sema_cur->count == buffer_capacity(channel->buffer))

和

if (type == RECV && sema_cur->count == 0) 就是为了保证本次signal的对象是select的send和receive，而不是channel_send和channel_receive，从而防止丢失唤醒。。

channel_select的主体就是一个循环，遍历channel_list的每一个channel，然后通过is_send这个变量进行判断是进行send还是receive。如果你对比select的循环主体和channel_send、channel_receive的写法，你会发现是高度的一致，无非就是send就是添加data，receive就是移除data，最后都需要进行signal（唤醒）一下receive和send 等待队列中的线程。接口都是统一的，但是统一的接口本身实现是复杂的。。

根据需求，select如果在遍历channel链表的过程中发现了可用的channel完成操作，那么就可以返回了（不过需要设置一下channel的下标）。

但是如果遍历了整个链表，发现没有一个channel满足条件，那么就需要设置一个共享的信号量，挂到每个channel的链表中。

channel_select_save_sema和channel_select_restore_sema是主要需要关心的接口，主要功能就是将信号量插入到channel_list的每个channel的链表中，或者是删除这个信号量。

在wait前插入，在wait被唤醒后移除。

你肯定会感到疑惑，为什么需要加一个ret == 0 的判断呢？你如果看一下channel_select_save_sema的实现，你就会发现这个判断就是为了保证不丢失唤醒，如果count中有值，那么我们就用count的唤醒信号，否则就用post提供的唤醒信号。

太trick了 :(

```c
int type = sel_cur.is_send;
if ((channel->select_count)[type])
{
    (channel->select_count)[type]--;
    Channel_Unlock(channel);
    return 0;
}
```

本次project是并发编程，不好调试（加一个printf都可能改变程序的运行状态，如果没有正确使用锁和信号量，程序每次的运行结构都可能不一样），而且题目要求不能加全局变量，emmm，否则加一个全局的锁可能会方便一些，不好理解是正常的，哪怕是写代码的人也不一定能完全理解程序的行为。





















