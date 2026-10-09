#ifndef VCOMPRESS_THREAD_POOL_H
#define VCOMPRESS_THREAD_POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ThreadPoolTaskFn)(void *arg);

typedef struct ThreadPool ThreadPool;

/* Create a thread pool with `num_threads` worker threads. If num_threads <= 1, single-threaded mode. */
ThreadPool *thread_pool_create(uint32_t num_threads);

/* Submit a task to the work queue */
bool thread_pool_submit(ThreadPool *pool, ThreadPoolTaskFn fn, void *arg);

/* Block until all queued tasks have finished executing */
void thread_pool_wait(ThreadPool *pool);

/* Destroy the thread pool, terminating worker threads and freeing memory */
void thread_pool_destroy(ThreadPool *pool);

/* Get number of worker threads */
uint32_t thread_pool_get_thread_count(const ThreadPool *pool);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_THREAD_POOL_H */
