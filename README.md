# CSCI-3753 Operating Systems: Programming Assigment 3
## Synopsis
This project is for my Operating Systems class. It covers safe multi-threading in C when dealing with shared resources.
The basic premise of the project is to setup a requester-resolver DNS system. We've been given the code for the actual
DNS resolution, but we're tasked with having the requesters read in from the input files, add the lines to a shared
buffer, and have the resolvers use the provided DNS functionality to resolve the addresses.

## Build
Just use the "make" command and it will build the `multi-lookup` executable.

## Usage
The command line arguments for the `multi-lookup` program are as follows:

`./multi-lookup [Number of requester threads] [Number of resolver threads] [Requester log file] [Resolver log file]
input1 input2....`

## Credit
This code was made by Jacob Malcy for CSCI-3753 (Spring 2020) at CU Boulder.

## License
MIT