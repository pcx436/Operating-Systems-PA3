order of approaching the assignment:
1. single requester thread that reads one file at a time and write to an array
1. create resolver thread that reads from shared array and writes result in results.txt file. Creates race condition with shared array. Use lock or semaphore or whatever. **NOTE** There is a limit for the size of the shared array in the writeup.
1. Create more requester threads. Ensure that threads don't open the same file. Probably don't want multiple threads reading different lines in the same file; it introduces a race condition.
1. Create more resolver threads that write to the file. Ensure they don't write over eachother.
1. Make the requester threads write to the file they must output to. Ensure the file is protected so they don't accidentally write eachother again.
