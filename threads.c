/**
 * User-level threads package.
 *
 * @author Ryan Strauss
 * @author Niall Williams
 */

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<setjmp.h>
#include<unistd.h>
#include<signal.h>
#include<sys/time.h>

#include "threads.h"

#define STACK_SIZE 65536
#define MAIN_THREAD 0

tcb_t thread_context[MAX_THREADS];
tcb_t *current_thread_context;
tcb_t *new_thread_context = NULL;
char stack[STACK_SIZE * MAX_THREADS];
int created = 0;

int preemption = 0;
struct itimerval start_timer;
struct itimerval zero_timer;

// Handler for when we create a thread
void sigusr_handler(int signal_number) {
    // When we make a thread, set the created flag and quit immediately
    if (setjmp(new_thread_context->buffer) == 0) {
        created = 1;
    }
        // Next time we long jump into the thread, start the function
    else {
        // Execute the function
        if (preemption) setitimer(ITIMER_REAL, &start_timer, NULL); // Reenable the alarm
        current_thread_context->function(current_thread_context->argument);
    }
}

// Handler for when the alarm goes off. The current thread yields.
void sigalrm_handler(int signal_number) {
    thread_yield();
}


void thread_init(int preemption_enabled) {
    // initialize the thread_context array (making all entries Invalid)
    for (int i = 0; i < MAX_THREADS; i++) {
        tcb_t new_struct;
        // create a first entry for the main program
        if (i == 0) {
            new_struct.function = NULL;
            new_struct.argument = NULL;
            new_struct.return_value = NULL;
            new_struct.state = STATE_ACTIVE;
        }

        new_struct.number = i;
        new_struct.stack = stack + (i * STACK_SIZE);
        new_struct.state = STATE_INVALID;

        thread_context[i] = new_struct;
    }

    // Make the main thread our current context
    current_thread_context = &thread_context[MAIN_THREAD];

    // Set up thread yield handler
    struct sigaction sigusr_init;
    memset(&sigusr_init, 0, sizeof(struct sigaction));
    sigusr_init.sa_handler = sigusr_handler;
    sigusr_init.sa_flags = SA_ONSTACK;
    sigemptyset(&sigusr_init.sa_mask);

    if (sigaction(SIGUSR1, &sigusr_init, NULL) == -1) {
        perror("sigaction/SIGUSR1");
        exit(EXIT_FAILURE);
    }

    // Set up the alarm signal handler
    if (preemption_enabled) {
        struct sigaction sigalrm_init;
        memset(&sigalrm_init, 0, sizeof(struct sigaction));
        sigalrm_init.sa_handler = sigalrm_handler;
        sigemptyset(&sigalrm_init.sa_mask);

        if (sigaction(SIGALRM, &sigalrm_init, NULL) == -1) {
            perror("sigaction/SIGALRM");
            exit(EXIT_FAILURE);
        }

        preemption = preemption_enabled;

        start_timer.it_interval.tv_sec = 0;
        start_timer.it_interval.tv_usec = 0;
        start_timer.it_value.tv_sec = 0;
        start_timer.it_value.tv_usec = 100000;

        zero_timer.it_interval.tv_sec = 0;
        zero_timer.it_interval.tv_usec = 0;
        zero_timer.it_value.tv_sec = 0;
        zero_timer.it_value.tv_usec = 0;

        setitimer(ITIMER_REAL, &start_timer, NULL); // Begin the timer
    }
}


int thread_create(void *(*function)(void *), void *argument) {
    for (int i = 1; i < MAX_THREADS; i++) {
        if (thread_context[i].state == STATE_INVALID) {
            thread_context[i].function = function;
            thread_context[i].argument = argument;
            thread_context[i].state = STATE_ACTIVE;
            thread_context[i].joiner_thread_number = -1; // Not waiting to join anything

            stack_t new_stack;
            new_stack.ss_flags = 0;
            new_stack.ss_size = STACK_SIZE;
            new_stack.ss_sp = thread_context[i].stack;

            if (sigaltstack(&new_stack, NULL) == -1) {
                perror("sigaltstack");
                exit(EXIT_FAILURE);
            }

            // pointer to the new thread buffer, so we setjmp into it in the handler
            new_thread_context = &thread_context[i];
            raise(SIGUSR1); // Sends a signal to yourself

            while (!created) {}; // Wait for the signal to be recieved to create the thread

            created = 0; // Reset creation flag for the next time we call thread_create

            return i;
        }
    }

    return -1; // no new thread created
}


int thread_yield(void) {
    if (preemption) setitimer(ITIMER_REAL, &zero_timer, NULL); // Disable the alarm
    int cur_index;

    for (int i = 1; i <= MAX_THREADS; i++) {
        cur_index = (i + current_thread_context->number) % MAX_THREADS;
        // When we have found a thread ready to activate
        if (thread_context[cur_index].state == STATE_ACTIVE) {
            if (setjmp(current_thread_context->buffer) == 0) {
                current_thread_context = &thread_context[cur_index];
                longjmp(current_thread_context->buffer, 1);
            }
                // Timer resets when we enter the next thread
            else {
                if (preemption) setitimer(ITIMER_REAL, &start_timer, NULL); // Reenable the alarm
            }

            return 1; // Sucessfully switched to new thread. Don't return index of thread because it could be 0
        }
    }

    return 0; // Failed to find any other active threads
}


void thread_join(int target_thread_number) {
    current_thread_context->state = STATE_BLOCKED;
    thread_context[target_thread_number].joiner_thread_number = current_thread_context->number;

    // switch to target_thread_number thread
    if (setjmp(current_thread_context->buffer) == 0) {
        current_thread_context = &thread_context[target_thread_number];
        setitimer(ITIMER_REAL, &start_timer, NULL);
        longjmp(current_thread_context->buffer, 1);
    }
        // Timer resets when we enter the next thread
    else {
        if (preemption) setitimer(ITIMER_REAL, &start_timer, NULL); // Reenable the alarm
    }
}


void thread_exit(void *return_value) {
    current_thread_context->state = STATE_FINISHED;
    current_thread_context->return_value = return_value;

    if (current_thread_context->joiner_thread_number != -1) {
        current_thread_context = &thread_context[current_thread_context->joiner_thread_number];
        current_thread_context->state = STATE_ACTIVE;
        setitimer(ITIMER_REAL, &start_timer, NULL);
        longjmp(current_thread_context->buffer, 1);
    }
    setitimer(ITIMER_REAL, &start_timer, NULL);
    thread_yield(); // Nothing else to do. Just exit
}
