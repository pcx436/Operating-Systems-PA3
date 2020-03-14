//
// Created by Jacob Malcy on 3/2/20.
//
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <util.h>
#include <stdlib.h>
#include <semaphore.h>

// LIMITS
#define MAX_INPUT_FILES 10
#define MAX_RESOLVER_THREADS 10
#define MAX_REQUESTER_THREADS 5
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

int dnsTest(){
    int ret;
    char s[30];

    ret = dnslookup("google.com", s, 30);
    if (ret == UTIL_SUCCESS){
        printf("%s\n", s);
        return 0;
    }
    else {
        fprintf(stderr, "DNS FAILURE!\n");
        return 1;
    }
}

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

void *requesterThread(void* args){
    size_t lineBuffSize = MAX_NAME_LENGTH * sizeof(char);
    ssize_t numReadBytes;

    struct threadArgs *reqArgs = (struct threadArgs*) args;
    pthread_mutex_t *accessLock = reqArgs->accessLock, *logLock = reqArgs->requesterLogLock;
    sem_t *space_available = reqArgs->space_available, *items_available = reqArgs->items_available;
    char *fName, *logName = reqArgs->requesterLog, *lineBuff = (char *) malloc(lineBuffSize);
    FILE *fp;
    int filesServiced = 0;

    // CRITICAL SECTION
    pthread_mutex_lock(accessLock);
    while(reqArgs->finishedInputs < reqArgs->numInputs) {
        fName = reqArgs->inputFiles[reqArgs->currentInput];
        reqArgs->currentInput += 1;
        pthread_mutex_unlock(accessLock);
        // END CRITICAL SECTION

        // TODO: deal with when more input files than threads
        printf("Requester %zu attempting to read file \"%s\"\n", pthread_self(), fName);
        fp = fopen(fName, "r");
        if (fp == NULL) {
            fprintf(stderr, "Requester %zu failed to open file \"%s\"!\n", pthread_self(), fName);
            free(lineBuff);

            return NULL;
        }

        while ((numReadBytes = getline(&lineBuff, &lineBuffSize, fp)) != -1) {
            if (lineBuff[numReadBytes - 1] == '\n') // remove ending newline character if found
                lineBuff[numReadBytes - 1] = '\0';

            // CRITICAL SECTION
            sem_wait(space_available);
            pthread_mutex_lock(accessLock);

            // Allocate space in the shared buffer and copy the line into it
            reqArgs->sharedBuffer[reqArgs->numInBuffer] = (char *) malloc(MAX_NAME_LENGTH * sizeof(char));
            strcpy(reqArgs->sharedBuffer[reqArgs->numInBuffer], lineBuff);
            printf("Requester %zu line: \"%s\", %zuB, index %d\n",
                   pthread_self(),
                   reqArgs->sharedBuffer[reqArgs->numInBuffer],
                   numReadBytes,
                   reqArgs->numInBuffer);
            reqArgs->numInBuffer += 1;

            pthread_mutex_unlock(accessLock);
            sem_post(items_available);
            // END CRITICAL SECTION
        }

        // CRITICAL SECTION - finished input
        pthread_mutex_lock(accessLock);
        reqArgs->finishedInputs++;
        pthread_mutex_unlock(accessLock);
        // END CRITICAL SECTION - finished input

        fclose(fp);
        filesServiced++;
    }
    free(lineBuff);

    // CRITICAL SECTION - writing log file
    pthread_mutex_lock(logLock);

    // open file
    fp = fopen(logName, "a");
    if(fp == NULL){
        pthread_mutex_unlock(logLock);
        fprintf(stderr, "Requester %zu could not open log file \"%s\"\n", pthread_self(), logName);

        // TODO: Change to ERR_BAD_REQ_LOG
        return NULL;
    }

    // write file
    if(fprintf(fp, "Requester %zu has serviced %d files\n", pthread_self(), filesServiced) < 0){
        fclose(fp);
        pthread_mutex_unlock(logLock);
        fprintf(stderr, "Requester %zu failed to write to log \"%s\"", pthread_self(), logName);

        // TODO: Change to some sort of write error
        return NULL;
    }

    fclose(fp);
    pthread_mutex_unlock(logLock);
    return NULL;
}

void *resolverThread(void* args){
    // variable declarations
    // max line length defined by max IP length (including null char) + a comma + max name length (including null char)
    // + newline + null
    const size_t max_line_length = (MAX_IP_LENGTH + MAX_NAME_LENGTH + 1) * sizeof(char);

    struct threadArgs *resArg = (struct threadArgs *)args;
    sem_t *space_available = resArg->space_available, *items_available = resArg->items_available;
    pthread_mutex_t *accessLock = resArg->accessLock;
    pthread_mutex_t *logLock = resArg->resolverLogLock;
    char *currentIP = (char *)malloc(MAX_IP_LENGTH * sizeof(char)), *currentName;

    char *lineToWrite = (char *)malloc(max_line_length);

    int resolutionResult;

    // CRITICAL SECTION - file access
    pthread_mutex_lock(accessLock);
    char *logFileName = resArg->resolverLog;
    pthread_mutex_unlock(accessLock);
    // END CRITICAL SECTION

    // CRITICAL SECTION - loop condition
    // sem_wait(items_available);
    pthread_mutex_lock(accessLock);

    while(resArg->finishedInputs < resArg->numInputs || resArg->numInBuffer != 0){
        printf("Resolver %zu will continue:\n", pthread_self());
        printf("\tcurrentInput\t%d\n", resArg->currentInput);
        printf("\tnumInputs\t%d\n", resArg->numInputs);
        printf("\tnumInBuffer\t%d\n", resArg->numInBuffer);

        pthread_mutex_unlock(accessLock);
        // END CRITICAL SECTION - loop condition

        // CRITICAL SECTION - shared buffer error check & name retrieval
        sem_wait(items_available);
        printf("Resolver %zu has determined there are items available\n", pthread_self());

        pthread_mutex_lock(accessLock);
        printf("Resolver %zu past access lock\n", pthread_self());

        // retrieve name
        currentName = resArg->sharedBuffer[resArg->numInBuffer - 1];
        resArg->numInBuffer--;

        pthread_mutex_unlock(accessLock);
        // END CRITICAL SECTION

        // resolve IP
        printf("Resolver %zu attempting to resolve \"%s\"\n", pthread_self(), currentName);
        resolutionResult = dnslookup(currentName, currentIP, MAX_IP_LENGTH);

        if(resolutionResult == UTIL_FAILURE){
            fprintf(stderr, "Failure in resolution of \"%s\"!\n", currentName);
            strcpy(currentIP, ""); // write empty string for resolved IP in log file
        }

        // build line to write to file
        strncpy(lineToWrite, currentName, MAX_NAME_LENGTH);
        strncat(lineToWrite, ",", sizeof(char));
        strncat(lineToWrite, currentIP, MAX_IP_LENGTH);

        // cleanup
        free(currentName);

        printf("Resolver %zu waiting to write line \"%s\"...\n", pthread_self(), lineToWrite);

        // CRITICAL SECTION - log writing
        pthread_mutex_lock(logLock);

        printf("Resolver %zu now writing line \"%s\"\n", pthread_self(), lineToWrite);

        FILE *fp = fopen(logFileName, "a");
        if(fp == NULL){
            pthread_mutex_unlock(logLock);
            // END CRITICAL SECTION - log writing
            fprintf(stderr, "Resolver %zu could not open log file \"%s\"!\n", pthread_self(), logFileName);
            free(currentIP);
            free(lineToWrite);

            // FIXME: Should somehow return ERR_BAD_FILE or something...
            return NULL;
        }
        printf("Resolver %zu successfully opened log file \"%s\"\n", pthread_self(), logFileName);

        // write to file
        if(fprintf(fp, "%s\n", lineToWrite) < 0){
            fclose(fp);
            pthread_mutex_unlock(logLock);
            // END CRITICAL SECTION - log writing

            fprintf(stderr, "Resolver %zu could not write \"%s\" to \"%s\"!\n",
                    pthread_self(),
                    lineToWrite,
                    logFileName);

            // cleanup
            free(currentIP);
            free(lineToWrite);

            // FIXME: Should return helpful error
            return NULL;
        }

        fclose(fp);
        pthread_mutex_unlock(logLock);
        // END CRITICAL SECTION - log writing
        printf("Resolver %zu successfully wrote line \"%s\" to log\n", pthread_self(), lineToWrite);

        sem_post(space_available);

        // CRITICAL SECTION - loop condition
        pthread_mutex_lock(accessLock);
    }
    printf("Resolver %zu final status:\n", pthread_self());
    printf("\tcurrentInput\t%d\n", resArg->currentInput);
    printf("\tnumInputs\t%d\n", resArg->numInputs);
    printf("\tnumInBuffer\t%d\n", resArg->numInBuffer);

    pthread_mutex_unlock(accessLock);
    // END CRITICAL SECTION - loop condition

    printf("Resolver %zu terminating\n", pthread_self());
    free(currentIP);
    free(lineToWrite);
    return NULL;
}

int main(int argc, char *argv[]){
    pthread_t requesterIDs[MAX_REQUESTER_THREADS];
    pthread_t resolverIDs[MAX_RESOLVER_THREADS];
    sem_t space_available, items_available;
    pthread_mutex_t accessLock, requesterLogLock, resolverLogLock;

    int i, numRequester, numResolver;
    int numInputs = argc > 5 ? argc - 5 : 0;
    char *trashPointer, *requesterLog, *resolverLog, *inputFiles[MAX_INPUT_FILES], *sharedBuffer[BUFFER_SIZE];

    if (argc < 6){
        fprintf(stderr, "Too few arguments!\n");
        return ERR_FEW_ARGS;
    }
    else if (numInputs > MAX_INPUT_FILES) {
        fprintf(stderr, "Too many input files, max %d!\n", MAX_INPUT_FILES);
        return ERR_NUM_INPUT;
    }

    // Parse CMD args
    numRequester = (int)strtol(argv[1], &trashPointer, BASE);
    numResolver = (int)strtol(argv[2], &trashPointer, BASE);
    requesterLog = argv[3];
    resolverLog = argv[4];

    if (numRequester > MAX_REQUESTER_THREADS){
        fprintf(stderr, "Exceeded max number of requester threads %d!\n", MAX_REQUESTER_THREADS);
        return ERR_NUM_REQUESTER;
    }
    else if (numResolver > MAX_RESOLVER_THREADS){
        fprintf(stderr, "Exceeded max number of resolver threads %d!\n", MAX_RESOLVER_THREADS);
        return ERR_NUM_RESOLVER;
    }

    // Grab the input files
    for (i = 0; i < numInputs; i++){
        inputFiles[i] = argv[5 + i];
    }

    // Debug outputs
    printf("Number of requesters: %d\n", numRequester);
    printf("Number of resolvers: %d\n", numResolver);
    printf("Requester log: %s\n", requesterLog);
    printf("Resolver log: %s\n", resolverLog);
    printf("Number of input files: %d\n", numInputs);
    printf("Input files:\n");
    for(i = 0; i < numInputs; i++)
        printf("\t%s\n", inputFiles[i]);

    // init semaphores & mutexes
    sem_init(&space_available, 0, BUFFER_SIZE);
    sem_init(&items_available, 0, 0);
    pthread_mutex_init(&accessLock, NULL);
    pthread_mutex_init(&requesterLogLock, NULL);
    pthread_mutex_init(&resolverLogLock, NULL);

    // create thread arg struct
    struct threadArgs tArgs;
    tArgs.numInputs = numInputs;
    tArgs.inputFiles = inputFiles;
    tArgs.currentInput = 0;
    tArgs.finishedInputs = 0;
    tArgs.numInBuffer = 0;
    tArgs.resolverLog = resolverLog;
    tArgs.requesterLog = requesterLog;
    tArgs.sharedBuffer = sharedBuffer;

    // assign semaphores
    tArgs.space_available = &space_available;
    tArgs.items_available = &items_available;

    // assign mutexes
    tArgs.accessLock = &accessLock;
    tArgs.resolverLogLock = &resolverLogLock;
    tArgs.requesterLogLock = &requesterLogLock;

    // TODO: Setup logging to file

    // Spawn requester threads
    for(i = 0; i < numRequester; i++){
        pthread_create(&requesterIDs[i], NULL, requesterThread, (void *)&tArgs);
    }

    // Spawn resolver threads
    for(i = 0; i < numResolver; i++){
        pthread_create(&resolverIDs[i], NULL, resolverThread, (void *)&tArgs);
    }

    // Join requester threads
    for(i = 0; i < numRequester; i++){
        pthread_join(requesterIDs[i], NULL);
    }

    // Join resolver threads
    for(i = 0; i < numResolver; i++){
        pthread_join(resolverIDs[i], NULL);
    }

    // TODO: Print timing information as requested

    return 0;
}
