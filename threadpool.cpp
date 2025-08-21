#include <stdio.h>
#include "threadpool.h"

static void *worker(void *arg) {
    ThreadPool *tp = (ThreadPool *)arg;
    while (true) {
        pthread_mutex_lock(&tp->mu);

        while (tp->queue.empty()) {
            pthread_cond_wait(&tp->not_empty, &tp->mu);
        }

        Work w = tp->queue.front();
        tp->queue.pop_front();
        pthread_mutex_unlock(&tp->mu);

        w.f(w.arg);
    }

    return NULL;
}

void thread_pool_init(ThreadPool *tp, size_t num_threads) {
    int rv = pthread_mutex_init(&tp->mu, NULL);
    if (rv != 0) {
        fprintf(stderr, "failed to create mutex\n");
        return;
    }

    rv = pthread_cond_init(&tp->not_empty, NULL);
    if (rv != 0) {
        fprintf(stderr, "failed to create conditional variable\n");
        return;
    }

    tp->threads.resize(num_threads);
    for (size_t i = 0; i < num_threads; i++) {
        int rv = pthread_create(&tp->threads[i], NULL, &worker, tp);
        if (rv != 0) {
            fprintf(stderr, "failed to create thread\n");
            return;
        }
    }
}

void thread_pool_queue(ThreadPool *tp, void (*f)(void *), void *arg) {
    pthread_mutex_lock(&tp->mu);
    tp->queue.push_back(Work {f, arg});
    pthread_cond_signal(&tp->not_empty);
    pthread_mutex_unlock(&tp->mu);
}