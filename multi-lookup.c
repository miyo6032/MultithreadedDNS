#include "multi-lookup.h"
#include "util.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

struct buffer_sync
{
	char ** buffer;
	pthread_mutex_t * read_block;
	pthread_mutex_t * write_lock;
	pthread_mutex_t * read_count_lock;
	pthread_mutex_t * empty_count_lock;
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
};

int write_to_buffer(char * line, struct requester_info * info)
{
	struct buffer_sync * sync = info->sync;
	pthread_mutex_lock(sync->empty_count_lock);
	if(sync->buffer_count == sync->buffer_size - 1)
	{
		pthread_cond_wait(sync->condp, sync->empty_count_lock);
	}
	pthread_mutex_lock(sync->read_block);
	pthread_mutex_lock(sync->write_lock);

	sync->buffer[sync->buffer_count] = line;

	pthread_mutex_unlock(sync->write_lock);
	pthread_mutex_unlock(sync->read_block);
	sync->buffer_count++;
	pthread_cond_signal(sync->condc);
	pthread_mutex_unlock(sync->empty_count_lock);
}

void * requester(void * ptr)
{
	struct requester_info * info = ((struct requester_info *)ptr);
	char * line = NULL;
	size_t len = 0;
	ssize_t line_length;
	int files_serviced = 0;
	int total_lines_serviced = 0;

	for(int i = 0; i < info->num_files; i++)
	{
		int lines_serviced = 0; // Keeps track if the thread has serviced at least one line of a  file
		while((line_length = getline(&line, &len, info->files[info->file_num]) != -1))
		{
			write_to_buffer(line, info);
			lines_serviced++;
		}

		if(lines_serviced > 0)
		{
			files_serviced++;
			total_lines_serviced += lines_serviced;
		}

		// Move to servicing the next file
		info->file_num = (info->file_num + 1) % info->num_files;
	}
	free(line);

	pthread_mutex_lock(info->serviced_write_lock);
	fprintf(info->requester_log, "Thread %ld serviced %d files and %d lines \n", syscall(SYS_gettid), files_serviced, total_lines_serviced);
	pthread_mutex_unlock(info->serviced_write_lock);

	pthread_exit(0);
}

FILE ** open_files(int num_files, const char ** input_file_names)
{
	FILE ** files = malloc(sizeof(files) * num_files);
	for(int i = 0; i < num_files; i++)
	{
		FILE * file = fopen(input_file_names[i], "r");
		if(file == NULL)
		{
			printf("File %s could not be loaded.", input_file_names[i]);
			return NULL;
		}
		files[i] = file;
	}
	return files;
}

void close_files(int num_files, FILE ** files)
{
	for(int i = 0; i < num_files; i++)
	{
		fclose(files[i]);
	}
}

int main(int argc, char const *argv[])
{
	int minimum_args = 6;
	int maximum_args = minimum_args + 9; // To bound the number of files
	int num_requesters;
	int num_resolvers;
	const char * requester_log_name;
	FILE * requester_log;
	const char * resolver_log_name;
	int num_files;
	const char ** input_file_names; 
	FILE ** files; // The files that are opened for the duration of the program
	pthread_t * requester_threads; // The requester_threads
	struct requester_info * requester_params_list; // The requester parameters
	pthread_mutex_t serviced_write_lock; // Enforces mutual exclusion on writing to "serviced.txt"
	int buffer_size = 32;
	char ** buffer;
	struct buffer_sync * sync;

	/*
	* All of these are for the bounded buffer problem + reader-writer problem
	*/
	pthread_mutex_t read_block;
	pthread_mutex_t write_lock;
	pthread_mutex_t read_count_lock;
	pthread_mutex_t empty_count_lock;
	pthread_cond_t condc;
	pthread_cond_t condp;

	if(argc < minimum_args || argc > maximum_args)
	{
		printf("Invalid number of arguments: Format is <# requester> <# resolver> <requester log> <resolver log> [ <data file> ...] with between 1 and 10 data files \n");
		return -1;
	}

	sscanf(argv[1], "%d", &num_requesters);
	sscanf(argv[2], "%d", &num_resolvers);
	requester_log_name = argv[3];
	resolver_log_name = argv[4];
	num_files = argc - 5;

	/*
	* Get the file names from command arguments
	*/
	input_file_names = (const char **)malloc(sizeof(char *) * num_files);
	for(int i = 5; i < argc; i++)
	{
		input_file_names[i - 5] = argv[i];
	}

	/*
	* Open and validate files
	*/
	files = open_files(num_files, input_file_names);
	if(files == NULL)
	{
		return -1;
	}

	/*
	* Open and validate requester log
	*/
	requester_log = fopen(requester_log_name, "w");
	if(requester_log == NULL)
	{
		printf("Invalid requester log name.\n");
		return -1;
	}

	if(num_resolvers > 10 || num_resolvers < 1 || num_requesters > 5 || num_requesters < 1)
	{
		printf("Invalid number of resolvers or requesters inputted. Please double check your arguments.\n");
		return -1;
	}

	/*
	* Print information about what the program is going to do
	*/
	printf("Processing %d files with %d requesters, %d, resolvers, with files:\n", num_files, num_requesters, num_resolvers);

	for(int i = 0; i < num_files; i++)
	{
		printf("%s\n", input_file_names[i]);
	}

	printf("Output will be place in %s for requesters, and %s for resolvers\n", requester_log_name, resolver_log_name);

	/**
	* Input Gathering ends here
	*/

	pthread_mutex_init(&serviced_write_lock, NULL);
	pthread_mutex_init(&read_block, NULL);
	pthread_mutex_init(&write_lock, NULL);
	pthread_mutex_init(&read_count_lock, NULL);
	pthread_mutex_init(&empty_count_lock, NULL);
	pthread_cond_init(&condc, NULL);
	pthread_cond_init(&condp, NULL);

	requester_threads = (pthread_t *)malloc(sizeof(pthread_t) * num_requesters);
	requester_params_list = (struct requester_info *)malloc(sizeof(* requester_params_list) * num_requesters);
	buffer = malloc((sizeof * buffer) * buffer_size);
	sync = malloc(sizeof * sync);

	sync->read_block = &read_block;
	sync->write_lock = &write_lock;
	sync->read_count_lock = &read_count_lock;
	sync->empty_count_lock = &empty_count_lock;
	sync->condc = &condc;
	sync->condp = &condp;
	sync->buffer = buffer;
	sync->buffer_count = 0;
	sync->buffer_size = buffer_size;

	for(int i = 0; i < num_requesters; i++)
	{
		/*
		* Initialize parameters for the requester threads
		*/
		requester_params_list[i].files = files;
		requester_params_list[i].file_num = i % num_files;
		requester_params_list[i].num_files = num_files;
		requester_params_list[i].thread = &requester_threads[i];
		requester_params_list[i].serviced_write_lock = &serviced_write_lock;
		requester_params_list[i].requester_log = requester_log;
		requester_params_list[i].sync = sync;
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_create(&requester_threads[i], NULL, requester, &requester_params_list[i]);
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_join(requester_threads[i], NULL);
	}

	printf("joined!\n");

	free(sync);
	free(buffer);
	free(requester_params_list);
	free(requester_threads);
	pthread_mutex_destroy(&serviced_write_lock);
	pthread_mutex_destroy(&read_block);
	pthread_mutex_destroy(&write_lock);
	pthread_mutex_destroy(&read_count_lock);
	pthread_mutex_destroy(&empty_count_lock);
	pthread_cond_destroy(&condc);
	pthread_cond_destroy(&condp);
	fclose(requester_log);
	close_files(num_files, files);
	free(files);
	free(input_file_names);

	return 0;
}