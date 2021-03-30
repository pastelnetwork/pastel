#pragma once
#ifndef _UNISTD_H
#define _UNISTD_H    1

#include <io.h>
#include <inttypes.h>
#include <time.h>
#include <BaseTsd.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME              0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC             1
#endif

#ifndef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID    2
#endif

#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID     3
#endif

#define isatty _isatty

typedef SSIZE_T ssize_t;

#ifndef _CLOCKID_T_DECLARED
typedef	unsigned long	clockid_t;
#define	_CLOCKID_T_DECLARED
#endif // _CLOCKID_T_DECLARED

// implemented in unistd.cpp
int gettimeofday(struct timeval* tp, struct timezone* tzp);
unsigned int sleep(unsigned int seconds);

#ifdef __cplusplus
extern "C" {
#endif

int clock_gettime(clockid_t clock_id, struct timespec* tp);

#ifdef __cplusplus
}
#endif

#endif
