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
    pthread_mutex_t *accessLock = reqArgs->accessLock;
    sem_t *space_available = reqArgs->space_available, *items_available = reqArgs->items_available;
    char *fName, *lineBuff;

    // CRITICAL SECTION
    pthread_mutex_lock(accessLock);
    int currentInput = reqArgs->currentInput;

    if(currentInput == reqArgs->numInputs){
        fprintf(stderr, "WARNING: Requester thread spawned with no more files to parse.\n");
        pthread_mutex_unlock(accessLock);

        return NULL;
    }

    fName = reqArgs->inputFiles[currentInput];
    reqArgs->currentInput += 1;
    pthread_mutex_unlock(accessLock);
    // END CRITICAL SECTION

    // TODO: deal with when more input files than threads
    printf("Requester attempting to read file \"%s\"\n", fName);
    FILE *fp = fopen(fName, "r");
    if (fp == NULL){
        fprintf(stderr, "Couldn't open file %s!\n", fName);
        return NULL;
    }

    lineBuff = (char *)malloc(lineBuffSize);
    while ((numReadBytes = getline(&lineBuff, &lineBuffSize, fp)) != -1){
        if(lineBuff[numReadBytes - 1] == '\n') // remove ending newline character if found
            lineBuff[numReadBytes - 1] = '\0';

        // CRITICAL SECTION
        sem_wait(space_available);
        pthread_mutex_lock(accessLock);

        // ensure we use the 0th index if adding first item
        if(reqArgs->numInBuffer == -1)
            reqArgs->numInBuffer = 0;

        // Allocate space in the shared buffer and copy the line into it
        reqArgs->sharedBuffer[reqArgs->numInBuffer] = (char *)malloc(MAX_NAME_LENGTH * sizeof(char));
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

    fclose(fp);
    free(lineBuff);

    return NULL;
}

void *resolverThread(void* args){
    // variable declarations
    // max line length defined by max IP length (including null char) + max name length (including null char) +
    // a comma + null char
    const size_t max_line_length = (MAX_IP_LENGTH + MAX_NAME_LENGTH) * sizeof(char);

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

    // FIXME: Bad practice to have while(1), fix with bool instead of break?
    while(1){
        // CRITICAL SECTION - shared buffer error check & name retrieval
        sem_wait(items_available);
        pthread_mutex_lock(accessLock);

        // TODO: Hopefully a temporary check while I flesh out this idea. Shouldn't get here without a race condition
        if(resArg->numInBuffer == -1){
            pthread_mutex_unlock(accessLock);
            fprintf(stderr, "Number of items in buffer is -1 but trying to take from buffer!\n");
            free(currentIP);
            free(lineToWrite);

            return NULL;
        }

        // retrieve name
        currentName = resArg->sharedBuffer[resArg->numInBuffer - 1];
        resArg->numInBuffer--;

        pthread_mutex_unlock(accessLock);
        // END CRITICAL SECTION

        // resolve IP
        printf("Attempting to resolve \"%s\"\n", currentName);
        resolutionResult = dnslookup(currentName, currentIP, MAX_IP_LENGTH);

        if(resolutionResult == UTIL_FAILURE){
            fprintf(stderr, "Failure in resolution of \"%s\"!\n", currentName);
            strcpy(currentName, ""); // write empty string for resolved IP in log file
        }

        // build line to write to file
        strncpy(lineToWrite, currentName, MAX_NAME_LENGTH);
        strncat(lineToWrite, ",", sizeof(char));
        strncat(lineToWrite, currentIP, MAX_IP_LENGTH);

        // cleanup
        free(currentName);

        printf("Resolver will attempt to write line \"%s\"\n", lineToWrite);

        // CRITICAL SECTION - log writing
        pthread_mutex_lock(logLock);

        FILE *fp = fopen(logFileName, "a");
        if(fp == NULL){
            pthread_mutex_unlock(logLock);
            // yes, it's access a shared resource, but it's not being modified so does it really matter?
            fprintf(stderr, "Could not open resolver results file \"%s\"!\n", logFileName);
            free(currentIP);
            free(lineToWrite);

            // FIXME: Should somehow return ERR_BAD_FILE or something...
            return NULL;
        }

        // write to file
        if(fputs(lineToWrite, fp) == EOF){
            fclose(fp);
            pthread_mutex_unlock(logLock);
            // END CRITICAL SECTION - log writing
            fprintf(stderr, "Could not write \"%s\" to \"%s\"!\n", currentIP, currentName);

            // cleanup
            free(currentIP);
            free(lineToWrite);

            // FIXME: Should return helpful error
            return NULL;
        }

        fclose(fp);
        pthread_mutex_unlock(logLock);
        // END CRITICAL SECTION - log writing

        // CRITICAL SECTION - loop condition
        pthread_mutex_lock(accessLock);

        // break if requesters have gone through all the files and nothing in shared buffer
        if(resArg->currentInput == resArg->numInputs && resArg->numInBuffer == -1)
            break;

        pthread_mutex_unlock(accessLock);
        sem_post(space_available);

        // END CRITICAL SECTION - loop condition
    }

    free(currentIP);
    free(lineToWrite);
    return NULL;
}

int main(int argc, char *argv[]){
    pthread_t requesterIDs[MAX_REQUESTER_THREADS];
    pthread_t resolverIDs[MAX_RESOLVER_THREADS];
    int i, numRequester, numResolver;
    int numInputs = argc > 5 ? argc - 5 : 0;
    char *ptr, *requesterLog, *resolverLog, *inputFiles[MAX_INPUT_FILES], *sharedBuffer[BUFFER_SIZE];
    sem_t space_available, items_available;
    pthread_mutex_t accessLock, requesterLogLock, resolverLogLock;

    if (argc < 6){
        fprintf(stderr, "Too few arguments!\n");
        return ERR_FEW_ARGS;
    }
    else if (numInputs > MAX_INPUT_FILES) {
        fprintf(stderr, "Too many input files, max %d!\n", MAX_INPUT_FILES);
        return ERR_NUM_INPUT;
    }

    // Parse CMD args
    numRequester = (int)strtol(argv[1], &ptr, BASE);
    numResolver = (int)strtol(argv[2], &ptr, BASE);
    requesterLog = argv[3];
    resolverLog = argv[4];

    if (numRequester > MAX_REQUESTER_THREADS){
        fprintf(stderr, "Max number of requester threads %d\n", MAX_REQUESTER_THREADS);
        return ERR_NUM_REQUESTER;
    }
    else if (numResolver > MAX_RESOLVER_THREADS){
        fprintf(stderr, "Max number of resolver threads %d\n", MAX_RESOLVER_THREADS);
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
    tArgs.numInBuffer = -1;
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
