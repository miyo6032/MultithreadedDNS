#include "multi-lookup.h"
#include "util.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

struct requester_info
{
	FILE ** files;
	int file_num;
	int num_files;
	pthread_t * thread;
	pthread_mutex_t * serviced_write_lock;
	FILE * requester_log;
};

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
			//printf("thread %ld read %s", syscall(SYS_gettid), line);
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

	// Does not need to be synchronized, because it is thread safe if multiple
	// threads in the same process write to the same file.
	// http://man7.org/linux/man-pages/man3/fopen.3.html
	fprintf(info->requester_log, "Thread %ld serviced %d files and %d lines \n", syscall(SYS_gettid), files_serviced, total_lines_serviced);

	pthread_exit(0);
}

struct requester_info * init_requester_info(int num_requesters, int num_files, FILE ** files, pthread_t * requester_threads, pthread_mutex_t * serviced_write_lock, FILE * requester_log)
{
	struct requester_info * params_list = (struct requester_info *)malloc(sizeof(* params_list) * num_requesters);

	for(int i = 0; i < num_requesters; i++)
	{
		/*
		* Initialize parameters for the requester threads
		*/
		params_list[i].files = files;
		params_list[i].file_num = i % num_files;
		params_list[i].num_files = num_files;
		params_list[i].thread = &requester_threads[i];
		params_list[i].serviced_write_lock = serviced_write_lock;
		params_list[i].requester_log = requester_log;
	}

	return params_list;
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

	requester_threads = (pthread_t *)malloc(sizeof(pthread_t) * num_requesters);
	requester_params_list = init_requester_info(num_requesters, num_files, files, requester_threads, &serviced_write_lock, requester_log);

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_create(&requester_threads[i], NULL, requester, &requester_params_list[i]);
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_join(requester_threads[i], NULL);
	}

	printf("joined!\n");

	free(requester_params_list);
	free(requester_threads);
	pthread_mutex_destroy(&serviced_write_lock);
	fclose(requester_log);
	close_files(num_files, files);
	free(files);
	free(input_file_names);

	return 0;
}