/**
 * A synchronization package to support multi-threading.
 *
 * @author Ryan Strauss
 * @author Niall Williams
 */

#include <stdlib.h>
#include <stdio.h>

#include "synchronization.h"
#include "threads.h"
#include "ll_double.h"

struct waiter_t {
    int thread_id;
};

int thread_mutex_init(thread_mutex_t *mutex) {
    if (mutex) {
        atomic_init(&(mutex->locked), 0);
    }

    return 0;
}

int thread_mutex_lock(thread_mutex_t *mutex) {
    while (1) {
        while (mutex->locked);

        int expected = 0;

        if (atomic_compare_exchange_strong(&(mutex->locked), &expected, 1)) {
            break;
        }
    }

    return 0;
}

int thread_mutex_unlock(thread_mutex_t *mutex) {
    atomic_store(&mutex->locked, 0);

    return 0;
}


int thread_cond_init(thread_cond_t *condition_variable) {
    struct list *waiters = (struct list *) malloc(sizeof(struct list));

    ll_init(waiters);
    condition_variable->waiters_list = *waiters;
    thread_mutex_init(&(condition_variable->internal_mutex));

    return 0;
}


int thread_cond_wait(thread_cond_t *condition_variable, thread_mutex_t *mutex) {
    thread_mutex_lock(&(condition_variable->internal_mutex));
    struct waiter_t *waiter = (struct waiter_t *) malloc(sizeof(struct waiter_t));

    if (waiter == NULL) {
        thread_mutex_unlock(&(condition_variable->internal_mutex));
        return 1;
    }

    waiter->thread_id = current_thread_context->number;
    ll_insert_tail(&(condition_variable->waiters_list), waiter);
    current_thread_context->state = STATE_BLOCKED;
    thread_mutex_unlock(mutex);
    thread_mutex_unlock(&(condition_variable->internal_mutex));
    thread_yield();
    thread_mutex_lock(mutex);

    return 0;
}


int thread_cond_signal(thread_cond_t *condition_variable) {
    thread_mutex_lock(&(condition_variable->internal_mutex));
    struct node *waiter_node = ll_remove_head(&(condition_variable->waiters_list));

    if (waiter_node == NULL) {
        thread_mutex_unlock(&(condition_variable->internal_mutex));
        return -1;
    }

    thread_context[((struct waiter_t *) waiter_node->data)->thread_id].state = STATE_ACTIVE;
    free(waiter_node->data);
    free(waiter_node);
    thread_mutex_unlock(&(condition_variable->internal_mutex));

    return 0;
}


int thread_cond_broadcast(thread_cond_t *condition_variable) {
    while (thread_cond_signal(condition_variable) == 0);
    return 0;
}
