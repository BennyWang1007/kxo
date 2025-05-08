#include <stdio.h>

#include "coroutine.h"


/* Create a new coroutine and return its ID */
int coroutine_create(void (*func)(void))
{
    if (coroutine_count >= MAX_COROUTINES) {
        printf("Error: Too many coroutines\n");
        return -1;
    }

    int id = coroutine_count++;
    coroutines[id].function = func;
    coroutines[id].active = 1;
    coroutines[id].id = id;

    return id;
}

/* Start the coroutine's function */
void coroutine_start(void)
{
    int id = current_coroutine;
    if (setjmp(coroutines[id].caller) == 0) {
        coroutines[id].function();
        coroutines[id].active = 0;
        printf("Coroutine %d completed\n", id);

        /* Return to the main program after completion */
        longjmp(coroutines[id].caller, 1);
    }
}

/* Yield control from the current coroutine back to the caller */
void coroutine_yield(void)
{
    int id = current_coroutine;
    if (setjmp(coroutines[id].context) == 0) {
        longjmp(coroutines[id].caller, 1); /* Return to the caller */
    }
}

/* Resume a coroutine's execution */
void coroutine_resume(int id)
{
    if (id < 0 || id >= coroutine_count || !coroutines[id].active) {
        printf("Error: Invalid coroutine ID or coroutine not active\n");
        return;
    }

    current_coroutine = id;

    /* First-time execution: start function */
    if (setjmp(coroutines[id].caller) == 0) {
        coroutine_start();
    } else {
        return; /* Returned here after yield or completion */
    }
}

/* Check if a coroutine is still active */
int coroutine_is_active(int id)
{
    if (id < 0 || id >= coroutine_count) {
        return 0;
    }
    return coroutines[id].active;
}