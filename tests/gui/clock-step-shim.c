/* A wall clock that steps when told to: gettimeofday() plus an offset that
 * fc_clock_step() moves. Preloaded into FreeCAD by the clock step test
 * (tests/gui/clock-step.py), which calls fc_clock_step() through ctypes at
 * the moment it wants the time of day to jump -- what a clock resync does
 * on its own schedule (WSL2: back about a second every half minute).
 *
 * Coin reads the time of day with gettimeofday() (SbTime::getTimeOfDay),
 * so this reaches every event stamp and every sensor. The steady clock is
 * clock_gettime(CLOCK_MONOTONIC) and is not touched. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/time.h>

static double offset;
static long calls;

void fc_clock_step(double seconds)
{
    offset += seconds;
}

/* How often the time of day was read: proof for the test that the shim is
 * in the path at all */
long fc_clock_calls(void)
{
    return calls;
}

int gettimeofday(struct timeval* restrict tv, void* restrict tz)
{
    static int (*real)(struct timeval*, void*);
    if (!real) {
        real = (int (*)(struct timeval*, void*))dlsym(RTLD_NEXT, "gettimeofday");
    }
    int r = real(tv, tz);
    ++calls;
    if (r == 0 && tv && offset != 0.0) {
        double t = (double)tv->tv_sec + (double)tv->tv_usec * 1e-6 + offset;
        tv->tv_sec = (long)t;
        tv->tv_usec = (long)((t - (double)tv->tv_sec) * 1e6);
    }
    return r;
}
