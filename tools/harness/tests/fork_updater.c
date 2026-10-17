#include "harness.h"
static int actions, threads;
static char updater_executable[1024], updater_directory[1024];
static int updater_state;
typedef void SDL_Thread;
#define HALO_BUILD_NUMBER 999
#define PATH_SEPARATOR "\\"
#define _updater_checking 1
#define _updater_handled 2
#define updater_check_thread NULL
#define platform_log(...) ((void)0)
#define config_boolean(n) 1
static char const *config_string(char const *name) { return "yes"; }
#define config_real(n) 0
#define SDL_SetAtomicInt(a,v) (*(a)=(v))
#define SDL_DetachThread(t) ((void)0)
static int update_executable_path(char *buffer, size_t size)
{
    actions++;
    snprintf(buffer, size, "C:\\Halo\\halo.exe");
    return 1;
}
static void updater_clean_up(void) { actions++; }
static SDL_Thread *SDL_CreateThread(void *callback, char const *name, void *context)
{
    threads++;
    return &threads;
}
#include "under_test.inc"
int main(int argc, char **argv)
{
    updater_start();
    CHECK(actions == 0 && threads == 0 && updater_state == 0,
        "numbered fork build must not touch updater files or start network checks, even with auto=true and answer=yes");
    return 0;
}
