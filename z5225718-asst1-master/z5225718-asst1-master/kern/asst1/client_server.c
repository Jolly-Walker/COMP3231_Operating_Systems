/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <kern/errno.h>
#include "client_server.h"


/*
 * Declare any variables you need here to implement and
 *  synchronise your queues and/or requests.
 */

struct my_node *queue_head;
struct my_node *queue_last;
struct my_node {
        request_t *req;
        struct my_node *next;
};
struct my_node *create_my_node(request_t *req);
void destroy_my_node(struct my_node *node);

struct my_node *create_my_node(request_t *req)
{
        struct my_node *new = (struct my_node *)kmalloc(sizeof(struct my_node));
        new->req = req;
        new->next = NULL;
        return new;
}

void destroy_my_node(struct my_node *node)
{
        kfree(node);
}

struct lock *my_queue_lock;
struct cv *queue_cv;
/* work_queue_enqueue():
 *
 * req: A pointer to a request to be processed. You can assume it is
 * a valid pointer or NULL. You can't assume anything about what it
 * points to, i.e. the internals of the request type.
 *
 * This function is expected to add requests to a single queue for
 * processing. The queue is a queue (FIFO). The function then returns
 * to the caller. It can be called concurrently by multiple threads.
 *
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required. 
 */

   

void work_queue_enqueue(request_t *req)
{

        struct my_node *in = create_my_node(req);

        lock_acquire(my_queue_lock);
        if (queue_last == NULL) {
                queue_head = in;
                queue_last = in;
        } else {
                queue_last->next = in;
                queue_last = in;
        }
        cv_signal(queue_cv, my_queue_lock);
        lock_release(my_queue_lock);
}

/* 
 * work_queue_get_next():
 *
 * This function is expected to block on a synchronisation primitive
 * until there are one or more requests in the queue for processing.
 *
 * A pointer to the request is removed from the queue and returned to
 * the server.
 * 
 * Note: The above is a high-level description of behaviour, not
 * detailed psuedo code. Depending on your implementation, more or
 * less code may be required.
 */


request_t *work_queue_get_next(void)
{
        lock_acquire(my_queue_lock);
        while(queue_head == NULL) {
                cv_wait(queue_cv, my_queue_lock);
        }
        struct my_node *tmp = queue_head;
        if (queue_head == queue_last) {
                queue_head = NULL;
                queue_last = NULL;
        } else {
                queue_head = queue_head->next;
        }

        request_t *data = tmp->req;
        destroy_my_node(tmp);
        lock_release(my_queue_lock);
        return data;
}




/*
 * work_queue_setup():
 * 
 * This function is called before the client and server threads are started. It is
 * intended for you to initialise any globals or synchronisation
 * primitives that are needed by your solution.
 *
 * In returns zero on success, or non-zero on failure.
 *
 * You can assume it is not called concurrently.
 */

int work_queue_setup(void)
{
        queue_head = NULL;
        queue_last = NULL;
        my_queue_lock = lock_create("my queue lock");
        queue_cv = cv_create("queue cv");
        if (my_queue_lock == NULL || queue_cv == NULL) {
                return ENOMEM;
        }
        return 0;

}


/* 
 * work_queue_shutdown():
 * 
 * This function is called after the participating threads have
 * exited. Use it to de-allocate or "destroy" anything allocated or created
 * on setup.
 *
 * You can assume it is not called concurrently.
 */

void work_queue_shutdown(void)
{
        lock_destroy(my_queue_lock);
        cv_destroy(queue_cv);
}
