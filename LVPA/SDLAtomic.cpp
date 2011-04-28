#include <SDL.h>
#include "SDLAtomic.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static SDL_mutex *mutex = NULL;

int SDL_Atomic_Incr(volatile int &i)
{
#ifdef _WIN32
    volatile LONG* dp = (volatile LONG*) &i;
    return InterlockedIncrement( dp );
#else

    if(!mutex)
        mutex = SDL_CreateMutex();

    SDL_mutexP(mutex);
    register int r = ++i;
    SDL_mutexV(mutex);

    return r;
#endif
}

int SDL_Atomic_Decr(volatile int &i)
{
#ifdef _WIN32
    volatile LONG* dp = (volatile LONG*) &i;
    return InterlockedDecrement( dp );
#else

    if(!mutex)
        mutex = SDL_CreateMutex();

    SDL_mutexP(mutex);
    register int r = --i;
    SDL_mutexV(mutex);

    return r;
#endif
}