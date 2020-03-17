//
// Created by jmalcy on 3/17/20.
//

#ifndef PA3_MULTI_LOOKUP_H
#define PA3_MULTI_LOOKUP_H

// LIMITS
#define MAX_INPUT_FILES 10
#define MAX_RESOLVER_THREADS 10
#define MAX_REQUESTER_THREADS 8
#define MAX_NAME_LENGTH 1025
#define MAX_IP_LENGTH INET6_ADDRSTRLEN

// ERRORS
#define ERR_NUM_INPUT 1
#define ERR_NUM_RESOLVER 2
#define ERR_NUM_REQUESTER 3
#define ERR_LEN_NAME 4
#define ERR_LEN_IP 5
#define ERR_FEW_ARGS 6
#define ERR_BAD_INPUT 7
#define ERR_BAD_REQ_LOG 8
#define ERR_BAD_RES_LOG 9

// CONSTANTS
#define BASE 10
#define BUFFER_SIZE 25

struct threadArgs {
    char **inputFiles;
    int numInputs;
    int currentInput;
    int finishedInputs;
    char *requesterLog;
    char *resolverLog;
    char **sharedBuffer;
    int numInBuffer;
    sem_t *space_available;
    sem_t *items_available;
    pthread_mutex_t *accessLock;
    pthread_mutex_t *resolverLogLock;
    pthread_mutex_t *requesterLogLock;
};

void *requsterThread(void* args);
void *resovlerThread(void* args);
#endif //PA3_MULTI_LOOKUP_H
