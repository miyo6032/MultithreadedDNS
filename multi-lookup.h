#ifndef MULTILOOKUP_H
#define MULTILOOKUP_H

#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <math.h>
#include "util.h"

#define MAX_LINE_LENGTH 1025
#define MAX_REQUESTER_THREADS 10
#define MAX_RESOLVER_THREADS 10
#define MAX_FILES 10
#define BUFFER_SIZE 16

/*
* Sync is used in both threads to safely read and write to the bounded buffer
*/
struct buffer_sync
{
	char ** buffer;
	pthread_mutex_t * mutex;
	pthread_cond_t * condc;
	pthread_cond_t * condp;
	int buffer_count;
	int buffer_size;
};

struct requester_info
{
	FILE ** files;
	int file_num;
	int num_files;
	pthread_t * thread;
	pthread_mutex_t * serviced_write_lock;
	FILE * requester_log;
	struct buffer_sync * sync;
};

struct resolver_info
{
	struct buffer_sync * sync;
	pthread_mutex_t * results_write_lock;
	int requester_threads_done;
	int read_count;
	FILE * resolver_log;
};

int write_to_buffer(struct requester_info * info);
char * read_from_buffer(struct resolver_info * info);
void * requester(void * ptr);
void * resolver(void * ptr);
void close_files(int num_files, FILE ** files);
FILE ** open_files(int num_files, const char ** input_file_names);

#endif
