//
// Created by Jacob Malcy on 3/2/20.
//
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <util.h>
#include <stdlib.h>
#include <semaphore.h>
// #include <arpa/inet.h>

// LIMITS
#define MAX_INPUT_FILES 10
#define MAX_RESOLVER_THREADS 10
#define MAX_REQUESTER_THREADS 5
#define MAX_NAME_LENGTH 1025
#define MAX_IP_LENGTH INET6_ADDRSTRLEN

// ERRORS
#define NUM_INPUT 1
#define NUM_RESOLVER 2
#define NUM_REQUESTER 3
#define LEN_NAME 4
#define LEN_IP 5
#define FEW_ARGS 6
#define BAD_INPUT 7

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

struct requestArg {
    char **inputFiles;
    int numInputs;
    char *logFile;
    char **sharedBuffer;
};

void *requesterThread(void* args){
    size_t lineBuffSize = MAX_NAME_LENGTH * sizeof(char);
    ssize_t numReadBytes;

    struct requestArg reqArgs = *(struct requestArg*) args;
    char *fName = reqArgs.inputFiles[0];

    FILE *fp = fopen(fName, "r");
    if (fp == NULL){
        fprintf(stderr, "Couldn't open file %s!\n", fName);
        return NULL;
    }

    char *lineBuff = (char *)malloc(lineBuffSize);
    while ((numReadBytes = getline(&lineBuff, &lineBuffSize, fp)) != -1){
        if(lineBuff[numReadBytes - 1] == '\n') // remove newline characters if found
            lineBuff[numReadBytes - 1] = '\0';
        printf("Line: \"%s\", %zu\n", lineBuff, numReadBytes);
    }

    fclose(fp);
    free(lineBuff);
    return NULL;
}

int main(int argc, char *argv[]){
    // return dnsTest();
    pthread_t requesterIDs[MAX_REQUESTER_THREADS];
    int numRequester, numResolver;
    int numInputs = argc > 5 ? argc - 5 : 0;
    int i;
    char *ptr, *requestLog, *resolveLog, *inputFiles[MAX_INPUT_FILES], *sharedBuffer[BUFFER_SIZE];

    if (argc < 6){
        fprintf(stderr, "Too few arguments!\n");
        return FEW_ARGS;
    }
    else if (numInputs > MAX_INPUT_FILES) {
        fprintf(stderr, "Too many input files, max %d!\n", MAX_INPUT_FILES);
        return NUM_INPUT;
    }

    // Parse CMD args
    numRequester = (int)strtol(argv[1], &ptr, BASE);
    numResolver = (int)strtol(argv[2], &ptr, BASE);
    requestLog = argv[3];
    resolveLog = argv[4];

    if (numRequester > MAX_REQUESTER_THREADS){
        fprintf(stderr, "Max number of requester threads %d\n", MAX_REQUESTER_THREADS);
        return NUM_REQUESTER;
    }
    else if (numResolver > MAX_RESOLVER_THREADS){
        fprintf(stderr, "Max number of resolver threads %d\n", MAX_RESOLVER_THREADS);
        return NUM_RESOLVER;
    }

    // Grab the input files
    for (i = 0; i < numInputs; i++){
        inputFiles[i] = argv[5 + i];
    }

    printf("Number of requesters: %d\n", numRequester);
    printf("Number of resolvers: %d\n", numResolver);
    printf("Requester log: %s\n", requestLog);
    printf("Resolver log: %s\n", resolveLog);
    printf("Number of input files: %d\n", numInputs);
    printf("Input files:\n");
    for(i = 0; i < numInputs; i++)
        printf("\t%s\n", inputFiles[i]);

    // create requester arg struct
    struct requestArg reqArgs;
    reqArgs.numInputs = numInputs;
    reqArgs.inputFiles = inputFiles;
    reqArgs.sharedBuffer = sharedBuffer;
    // TODO: Setup logging to file

    // TODO: create resolver arg struct

    // Spawn requester threads
    for(i = 0; i < numRequester; i++){
        pthread_create(&requesterIDs[i], NULL, requesterThread, (void *)&reqArgs);
    }

    // Join requester threads
    for(i = 0; i < numRequester; i++){
        pthread_join(requesterIDs[i], NULL);
    }

    return 0;
}
