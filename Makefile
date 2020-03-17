CC=gcc
CFLAGS=-lpthread

multi-lookup: multi-lookup.c multi-lookup.h util.c util.h
	$(CC) -o multi-lookup -I. multi-lookup.c util.c -lpthread
