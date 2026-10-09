#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <sys/resource.h>
#include <sys/time.h>
#endif

double utils_get_time_sec(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static bool freq_init = false;
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

void utils_format_bytes(uint64_t bytes, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    const double kb = 1024.0;
    const double mb = kb * 1024.0;
    const double gb = mb * 1024.0;

    if ((double)bytes >= gb) {
        snprintf(buf, buf_size, "%.1f GB", (double)bytes / gb);
    } else if ((double)bytes >= mb) {
        snprintf(buf, buf_size, "%.1f MB", (double)bytes / mb);
    } else if ((double)bytes >= kb) {
        snprintf(buf, buf_size, "%.1f KB", (double)bytes / kb);
    } else {
        snprintf(buf, buf_size, "%llu B", (unsigned long long)bytes);
    }
}

size_t utils_get_peak_rss(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return (size_t)info.PeakWorkingSetSize;
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        /* On Linux ru_maxrss is in KB, on macOS in bytes */
#if defined(__APPLE__)
        return (size_t)usage.ru_maxrss;
#else
        return (size_t)usage.ru_maxrss * 1024;
#endif
    }
    return 0;
#endif
}

char *utils_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}
