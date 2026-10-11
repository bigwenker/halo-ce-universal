#include "harness.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct { int value; } SDL_AtomicInt;
#include "under_test.inc"

static SDL_AtomicInt counter;

static void *increment(void *unused)
{
    int i, previous;
    (void)unused;
    for (i = 0; i < 5000; i++)
        do {
            previous = __atomic_load_n(&counter.value, __ATOMIC_SEQ_CST);
        } while (!SDL_CompareAndSwapAtomicInt(&counter, previous, previous + 1));
    return NULL;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2, "case required");
    char const *case_name = argv[1];
    CASE("values") {
        SDL_AtomicInt value = {0};
        CHECK(SDL_SetAtomicInt(&value, 1) == 0 && value.value == 1, "set must return previous value");
        CHECK(!SDL_CompareAndSwapAtomicInt(&value, 0, 2) && value.value == 1, "failed compare changed value");
        CHECK(SDL_CompareAndSwapAtomicInt(&value, 1, 0) && value.value == 0, "chat request not consumed");
        CHECK(!SDL_CompareAndSwapAtomicInt(&value, 1, 0), "chat request consumed twice");
        CHECK(SDL_SetAtomicInt(&value, -3) == 0 && SDL_SetAtomicInt(&value, 7) == -3, "signed previous value lost");
    }
    else CASE("threads") {
        pthread_t threads[8];
        int i;
        for (i = 0; i < 8; i++) CHECK(!pthread_create(&threads[i], NULL, increment, NULL), "thread creation");
        for (i = 0; i < 8; i++) CHECK(!pthread_join(threads[i], NULL), "thread join");
        CHECK(counter.value == 40000, "atomic increments lost");
    }
    else return 2;
    return 0;
}
