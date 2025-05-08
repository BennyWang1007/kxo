#include <setjmp.h>

#define MAX_COROUTINES 10
#define STACK_SIZE 4096


typedef struct {
    jmp_buf context;        /* Stores the context of the coroutine */
    jmp_buf caller;         /* Stores the context of the caller */
    char stack[STACK_SIZE]; /* Stack for the coroutine */
    int active;             /* Whether the coroutine is active */
    void (*function)(void); /* The function to run as a coroutine */
    int id;                 /* Unique ID for the coroutine */
} coroutine_t;

/* Global variables for coroutine management */
static coroutine_t coroutines[MAX_COROUTINES];
static int current_coroutine = -1; /* Currently executing coroutine */
static int coroutine_count = 0;    /* Number of registered coroutines */

/* Forward declarations */
void coroutine_start(void);

/* Create a new coroutine and return its ID */
int coroutine_create(void (*func)(void));

/* Start the coroutine's function */
void coroutine_start(void);

/* Yield control from the current coroutine back to the caller */
void coroutine_yield(void);

/* Resume a coroutine's execution */
void coroutine_resume(int id);

/* Check if a coroutine is still active */
int coroutine_is_active(int id);
