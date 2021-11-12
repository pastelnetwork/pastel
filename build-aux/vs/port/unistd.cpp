#include <chrono>
#include <WinSock2.h>
#include <unistd.h>
using namespace std::chrono;

int gettimeofday(struct timeval* tp, struct timezone* tzp)
{
	const system_clock::duration d = system_clock::now().time_since_epoch();
	const seconds s = duration_cast<seconds>(d);
	tp->tv_sec = static_cast<decltype(tp->tv_sec)>(s.count());
	tp->tv_usec = static_cast<decltype(tp->tv_usec)>(duration_cast<microseconds>(d - s).count());
    return 0;
}

/**
 * sleep() causes the calling thread to sleep either until the
 * number of real-time seconds specified in seconds have elapsed or
 * until a signal arrives which is not ignored.
 * 
 * \param seconds - specified number of seconds
 * \return - 0, Sleep can't be interrupted.
 */
unsigned int sleep(unsigned int seconds)
{
    // accepts time in milliseconds
    ::Sleep(seconds * 1000);
    return 0;
}

constexpr int64_t POW10_7 = 10000000;
constexpr int64_t POW10_9 = 1000000000;

/* Number of 100ns-seconds between the beginning of the Windows epoch
 * (Jan. 1, 1601) and the Unix epoch (Jan. 1, 1970)
 */
constexpr int64_t DELTA_EPOCH_IN_100NS = 116444736000000000;

static int lc_set_errno(const int result)
{
    if (result != 0)
    {
        errno = result;
        return -1;
    }
    return 0;
}

/**
 * Get the time of the specified clock clock_id and stores it in the struct
 * timespec pointed to by tp.
 * @param  clock_id The clock_id argument is the identifier of the particular
 *         clock on which to act. The following clocks are supported:
 * <pre>
 *     CLOCK_REALTIME  System-wide real-time clock. Setting this clock
 *                 requires appropriate privileges.
 *     CLOCK_MONOTONIC Clock that cannot be set and represents monotonic
 *                 time since some unspecified starting point.
 *     CLOCK_PROCESS_CPUTIME_ID High-resolution per-process timer from the CPU.
 *     CLOCK_THREAD_CPUTIME_ID  Thread-specific CPU-time clock.
 * </pre>
 * @param  tp The pointer to a timespec structure to receive the time.
 * @return If the function succeeds, the return value is 0.
 *         If the function fails, the return value is -1,
 *         with errno set to indicate the error.
 */
int clock_gettime(clockid_t clock_id, struct timespec* tp)
{
    unsigned __int64 t;
    LARGE_INTEGER pf, pc;
    union {
        unsigned __int64 u64;
        FILETIME ft;
    }  ct, et, kt, ut;

    switch (clock_id) {
    case CLOCK_REALTIME:
    {
        GetSystemTimeAsFileTime(&ct.ft);
        t = ct.u64 - DELTA_EPOCH_IN_100NS;
        tp->tv_sec = t / POW10_7;
        tp->tv_nsec = ((int)(t % POW10_7)) * 100;

        return 0;
    }

    case CLOCK_MONOTONIC:
    {
        if (QueryPerformanceFrequency(&pf) == 0)
            return lc_set_errno(EINVAL);

        if (QueryPerformanceCounter(&pc) == 0)
            return lc_set_errno(EINVAL);

        tp->tv_sec = pc.QuadPart / pf.QuadPart;
        tp->tv_nsec = (int)(((pc.QuadPart % pf.QuadPart) * POW10_9 + (pf.QuadPart >> 1)) / pf.QuadPart);
        if (tp->tv_nsec >= POW10_9) {
            tp->tv_sec++;
            tp->tv_nsec -= POW10_9;
        }

        return 0;
    }

    case CLOCK_PROCESS_CPUTIME_ID:
    {
        if (0 == GetProcessTimes(GetCurrentProcess(), &ct.ft, &et.ft, &kt.ft, &ut.ft))
            return lc_set_errno(EINVAL);
        t = kt.u64 + ut.u64;
        tp->tv_sec = t / POW10_7;
        tp->tv_nsec = ((int)(t % POW10_7)) * 100;

        return 0;
    }

    case CLOCK_THREAD_CPUTIME_ID:
    {
        if (0 == GetThreadTimes(GetCurrentThread(), &ct.ft, &et.ft, &kt.ft, &ut.ft))
            return lc_set_errno(EINVAL);
        t = kt.u64 + ut.u64;
        tp->tv_sec = t / POW10_7;
        tp->tv_nsec = ((int)(t % POW10_7)) * 100;

        return 0;
    }

    default:
        break;
    }

    return lc_set_errno(EINVAL);
}

