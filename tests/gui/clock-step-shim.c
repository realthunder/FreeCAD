/* A wall clock that steps when told to: the time of day plus an offset
 * that fc_clock_step() moves, or that a schedule moves by itself. Preloaded
 * into FreeCAD by the clock step tests (tests/gui/clock-step.py,
 * tests/gui/drain-clock-step.py), which call it through ctypes -- what a
 * clock resync does on its own schedule (WSL2 with two time masters: back
 * about a second every half minute).
 *
 * Both readings of the time of day are covered:
 *
 * - gettimeofday(), which Coin reads (SbTime::getTimeOfDay): every event
 *   stamp and every sensor;
 * - clock_gettime(CLOCK_REALTIME), which is what libstdc++'s
 *   std::chrono::system_clock reads -- and high_resolution_clock, which
 *   is the same clock there.
 *
 * The steady clock is clock_gettime(CLOCK_MONOTONIC) and is passed on
 * untouched.
 *
 * The schedule is for a step that has to fall INSIDE something the test
 * cannot interrupt -- a slice of work on the thread the test itself runs
 * on. Its steps are a function of the steady clock and of nothing else,
 * so a step is made at its time whether or not anybody is reading. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static double offset;
static long calls;
static long reads;

/* The schedule: `sched_count` steps of `sched_amount` seconds, the first
 * at `sched_first` on the steady clock, one every `sched_every` seconds */
static double sched_amount;
static double sched_first;
static double sched_every;
static volatile int sched_count;

static int (*real_clock_gettime)(clockid_t, struct timespec*);

static int raw_clock_gettime(clockid_t id, struct timespec* ts)
{
    if (!real_clock_gettime) {
        /* Looked up once; until it is found -- the lookup may itself read
         * a clock -- the kernel is asked directly */
        static int looking;
        if (looking) {
            return (int)syscall(SYS_clock_gettime, id, ts);
        }
        looking = 1;
        real_clock_gettime =
            (int (*)(clockid_t, struct timespec*))dlsym(RTLD_NEXT, "clock_gettime");
        looking = 0;
        if (!real_clock_gettime) {
            return (int)syscall(SYS_clock_gettime, id, ts);
        }
    }
    return real_clock_gettime(id, ts);
}

static double steady(void)
{
    struct timespec ts;
    raw_clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* How many of the schedule's steps have been made by now */
static long stepped(void)
{
    int count = sched_count;
    if (count <= 0) {
        return 0;
    }
    double t = steady();
    if (t < sched_first) {
        return 0;
    }
    long n = (long)((t - sched_first) / sched_every) + 1;
    return n > count ? count : n;
}

static double shift(void)
{
    return offset + sched_amount * (double)stepped();
}

void fc_clock_step(double seconds)
{
    offset += seconds;
}

/* `count` steps of `seconds`, the first `delay` seconds from now and one
 * every `every` seconds after it. Replaces a schedule that is running. */
void fc_clock_schedule(double seconds, double delay, double every, int count)
{
    sched_count = 0;
    sched_amount = seconds;
    sched_first = steady() + delay;
    sched_every = every > 0.0 ? every : 1.0;
    sched_count = count;
}

/* End the schedule and take its steps back: the time of day is where it
 * was before the schedule, as after a resync that has caught up. Answers
 * how many steps were made. */
long fc_clock_unschedule(void)
{
    long n = stepped();
    sched_count = 0;
    return n;
}

/* How often the time of day was read: proof for the test that the shim is
 * in the path at all */
long fc_clock_calls(void)
{
    return calls;
}

/* The same for clock_gettime(CLOCK_REALTIME) */
long fc_clock_reads(void)
{
    return reads;
}

int gettimeofday(struct timeval* restrict tv, void* restrict tz)
{
    static int (*real)(struct timeval*, void*);
    if (!real) {
        real = (int (*)(struct timeval*, void*))dlsym(RTLD_NEXT, "gettimeofday");
    }
    int r = real(tv, tz);
    ++calls;
    if (r == 0 && tv) {
        double by = shift();
        if (by != 0.0) {
            long long us = (long long)tv->tv_sec * 1000000LL + tv->tv_usec
                + (long long)(by * 1e6);
            tv->tv_sec = (time_t)(us / 1000000LL);
            tv->tv_usec = (suseconds_t)(us % 1000000LL);
        }
    }
    return r;
}

int clock_gettime(clockid_t id, struct timespec* ts)
{
    int r = raw_clock_gettime(id, ts);
    if (id != CLOCK_REALTIME) {
        return r;
    }
    ++reads;
    if (r == 0 && ts) {
        double by = shift();
        if (by != 0.0) {
            long long ns = (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec
                + (long long)(by * 1e9);
            ts->tv_sec = (time_t)(ns / 1000000000LL);
            ts->tv_nsec = (long)(ns % 1000000000LL);
        }
    }
    return r;
}
