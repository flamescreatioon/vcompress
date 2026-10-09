#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_QUEUE_SIZE 1024

typedef struct Task {
    ThreadPoolTaskFn fn;
    void *arg;
} Task;

struct ThreadPool {
    pthread_t *threads;
    uint32_t num_threads;

    Task queue[MAX_QUEUE_SIZE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;

    size_t active_tasks;
    bool stop;

    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    pthread_cond_t idle;
};

static void *worker_thread_func(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;

    while (true) {
        pthread_mutex_lock(&pool->lock);

        while (pool->queue_count == 0 && !pool->stop) {
            pthread_cond_wait(&pool->not_empty, &pool->lock);
        }

        if (pool->stop && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        /* Dequeue task */
        Task task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % MAX_QUEUE_SIZE;
        pool->queue_count--;
        pool->active_tasks++;

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->lock);

        /* Execute task outside lock */
        if (task.fn) {
            task.fn(task.arg);
        }

        pthread_mutex_lock(&pool->lock);
        pool->active_tasks--;
        if (pool->queue_count == 0 && pool->active_tasks == 0) {
            pthread_cond_broadcast(&pool->idle);
        }
        pthread_mutex_unlock(&pool->lock);
    }

    return NULL;
}

ThreadPool *thread_pool_create(uint32_t num_threads) {
    if (num_threads == 0) num_threads = 1;

    ThreadPool *pool = (ThreadPool *)calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pool->active_tasks = 0;
    pool->stop = false;

    if (pthread_mutex_init(&pool->lock, NULL) != 0 ||
        pthread_cond_init(&pool->not_empty, NULL) != 0 ||
        pthread_cond_init(&pool->not_full, NULL) != 0 ||
        pthread_cond_init(&pool->idle, NULL) != 0) {
        free(pool);
        return NULL;
    }

    if (num_threads > 1) {
        pool->threads = (pthread_t *)calloc(num_threads, sizeof(pthread_t));
        if (!pool->threads) {
            pthread_mutex_destroy(&pool->lock);
            pthread_cond_destroy(&pool->not_empty);
            pthread_cond_destroy(&pool->not_full);
            pthread_cond_destroy(&pool->idle);
            free(pool);
            return NULL;
        }

        for (uint32_t i = 0; i < num_threads; i++) {
            if (pthread_create(&pool->threads[i], NULL, worker_thread_func, pool) != 0) {
                /* Failed creating thread: trigger shutdown and cleanup */
                pool->stop = true;
                pthread_cond_broadcast(&pool->not_empty);
                for (uint32_t j = 0; j < i; j++) {
                    pthread_join(pool->threads[j], NULL);
                }
                free(pool->threads);
                pthread_mutex_destroy(&pool->lock);
                pthread_cond_destroy(&pool->not_empty);
                pthread_cond_destroy(&pool->not_full);
                pthread_cond_destroy(&pool->idle);
                free(pool);
                return NULL;
            }
        }
    }

    return pool;
}

bool thread_pool_submit(ThreadPool *pool, ThreadPoolTaskFn fn, void *arg) {
    if (!pool || !fn) return false;

    if (pool->num_threads <= 1) {
        /* Direct synchronous execution */
        fn(arg);
        return true;
    }

    pthread_mutex_lock(&pool->lock);

    while (pool->queue_count >= MAX_QUEUE_SIZE && !pool->stop) {
        pthread_cond_wait(&pool->not_full, &pool->lock);
    }

    if (pool->stop) {
        pthread_mutex_unlock(&pool->lock);
        return false;
    }

    pool->queue[pool->queue_tail].fn = fn;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % MAX_QUEUE_SIZE;
    pool->queue_count++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
    return true;
}

void thread_pool_wait(ThreadPool *pool) {
    if (!pool) return;
    if (pool->num_threads <= 1) return;

    pthread_mutex_lock(&pool->lock);
    while (pool->queue_count > 0 || pool->active_tasks > 0) {
        pthread_cond_wait(&pool->idle, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;

    if (pool->num_threads > 1) {
        pthread_mutex_lock(&pool->lock);
        pool->stop = true;
        pthread_cond_broadcast(&pool->not_empty);
        pthread_mutex_unlock(&pool->lock);

        for (uint32_t i = 0; i < pool->num_threads; i++) {
            pthread_join(pool->threads[i], NULL);
        }
        free(pool->threads);
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_cond_destroy(&pool->idle);
    free(pool);
}

uint32_t thread_pool_get_thread_count(const ThreadPool *pool) {
    return pool ? pool->num_threads : 1;
}
