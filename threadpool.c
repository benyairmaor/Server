#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * threadpool.h
 *
 * This file declares the functionality associated with
 * your implementation of a threadpool.
 */

// maximum number of threads allowed in a pool

/**
 * create_threadpool creates a fixed-sized thread
 * pool.  If the function succeeds, it returns a (non-NULL)
 * "threadpool", else it returns NULL.
 * this function should:
 * 1. input sanity check 
 * 2. initialize the threadpool structure
 * 3. initialized mutex and conditional variables
 * 4. create the threads, the thread init function is do_work and its argument is the initialized threadpool. 
 */

threadpool* create_threadpool(int num_threads_in_pool){
    int x = 0;
    if(num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool < 0){
        fprintf(stderr, "Usage: threadpool %d <max-number-of-jobs>\n", num_threads_in_pool);
        return NULL;
    }
    threadpool *toReturn = (threadpool*)calloc(1,sizeof(threadpool));
    if(toReturn == NULL){
        perror("malloc failed\n");
        return NULL;
    }
    toReturn->num_threads = num_threads_in_pool;
    toReturn->threads = (pthread_t*)calloc(toReturn->num_threads, sizeof(pthread_t));
    if(toReturn->threads == NULL){
        free(toReturn);
        perror("malloc failed\n");
        return NULL;
    }
    toReturn->qsize = 0;
    x = pthread_mutex_init(&toReturn->qlock,NULL);
    if(x != 0){
        perror("mutex failed\n");
        free(toReturn->threads);
        free(toReturn);
        return NULL;
    }
    x = pthread_cond_init(&toReturn->q_empty, NULL);
    if(x != 0){
        perror("mutex failed\n");
        free(toReturn->threads);
        free(toReturn);
        return NULL;
    }
    x = pthread_cond_init(&toReturn->q_not_empty, NULL);
    if(x != 0){
        perror("mutex failed\n");
        free(toReturn->threads);
        free(toReturn);
        return NULL;
    }
    toReturn->qhead = NULL;
    toReturn->qtail = NULL;
    toReturn->shutdown = 0;
    toReturn->dont_accept = 0;
    for(int i = 0; i < toReturn->num_threads; i++){
        pthread_create(&toReturn->threads[i], NULL, do_work, toReturn);
    }
    return toReturn;
}

// "dispatch_fn" declares a typed function pointer.  A
// variable of type "dispatch_fn" points to a function
// with the following signature:
// 
//     int dispatch_function(void *arg);

void dispatch(threadpool* threadPool, dispatch_fn disPatch, void *arg){
    work_t *work = (work_t*)malloc(sizeof(work_t));
    pthread_mutex_lock(&threadPool->qlock);
    if(threadPool->dont_accept == 0){
        work->routine = disPatch;
        work->arg = arg;
        work->next = NULL;
        if(threadPool->qsize == 0){
            threadPool->qtail = work;
            threadPool->qhead = work;
        }
        else{
            threadPool->qtail->next = work;
            threadPool->qtail = work;
        }
        threadPool->qsize++;
    }
    pthread_mutex_unlock(&threadPool->qlock);
    pthread_cond_signal(&threadPool->q_not_empty);
}

/**
 * The work function of the thread
 * this function should:
 * 1. lock mutex
 * 2. if the queue is empty, wait
 * 3. take the first element from the queue (work_t)
 * 4. unlock mutex
 * 5. call the thread routine
 *
 */

void* do_work(void* threadPool){
    threadpool* Pool = (threadpool*) threadPool; 
    while(1){
        pthread_mutex_lock(&Pool->qlock);
        if(Pool->shutdown == 1){
            pthread_mutex_unlock(&Pool->qlock);
            return NULL;
        } 
        if(Pool->qsize == 0){
            pthread_cond_wait(&Pool->q_not_empty, &Pool->qlock);
        }
        if(Pool->shutdown == 1){
            pthread_mutex_unlock(&Pool->qlock);
            return NULL;
        } 
        work_t *toDo = Pool->qhead;
        if(Pool->qsize == 1){
            Pool->qhead = NULL;
            Pool->qtail = NULL;
            Pool->qsize--;
        }
        else if (Pool->qsize > 1){
            Pool->qhead = Pool->qhead->next;
            Pool->qsize--;
        }
        if(Pool->qsize == 0 && Pool->dont_accept == 1){
            pthread_cond_signal(&Pool->q_empty);
        }
        pthread_mutex_unlock(&Pool->qlock);
        if(toDo != NULL){
            toDo->routine(toDo->arg);
            free(toDo);
        }
    }
}

/**
 * destroy_threadpool kills the threadpool, causing
 * all threads in it to commit suicide, and then
 * frees all the memory associated with the threadpool.
 */

void destroy_threadpool(threadpool* threadPool){
    pthread_mutex_lock(&threadPool->qlock);
    threadPool->dont_accept = 1;
    if(threadPool->qsize > 0){
        pthread_cond_wait(&threadPool->q_empty, &threadPool->qlock);
    }
    threadPool->shutdown = 1;
    pthread_mutex_unlock(&threadPool->qlock);
    pthread_cond_broadcast(&threadPool->q_not_empty);
    pthread_cond_broadcast(&threadPool->q_empty);
    for(int i = 0; i < threadPool->num_threads; i++){
        pthread_join(threadPool->threads[i], NULL);
    }
    pthread_cond_destroy(&threadPool->q_empty);
    pthread_cond_destroy(&threadPool->q_not_empty);
    pthread_mutex_destroy(&threadPool->qlock);
    free(threadPool->threads);
    free(threadPool);
}