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
	pthread_t * thread;
};

void * requester(void * ptr)
{
	struct requester_info * info = ((struct requester_info *)ptr);
	char * line = NULL;
	size_t len = 0;
	ssize_t line_length;

	while((line_length = getline(&line, &len, info->files[info->file_num]) != -1))
	{
		printf("thread %d read %s", syscall(SYS_gettid), line);
	}

	free(line);

	pthread_exit(0);
}

struct requester_info * init_requester_info(int num_requesters, int num_files, FILE ** files, pthread_t * requester_threads)
{
	struct requester_info * params_list = (struct requester_info *)malloc(sizeof(* params_list) * num_requesters);

	for(int i = 0; i < num_requesters; i++)
	{
		/*
		* Initialize parameters for the requester threads
		*/
		params_list[i].files = files;
		params_list[i].file_num = i % num_files;
		params_list[i].thread = &requester_threads[i];
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
	const char * resolver_log_name;
	int num_files;
	const char ** input_file_names; 
	FILE ** files; // The files that are opened for the duration of the program
	pthread_t * requester_threads; // The requester_threads
	struct requester_info * requester_params_list; // The requester parameters

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

	if(num_resolvers > 10 || num_resolvers < 1 || num_requesters > 5 || num_requesters < 1)
	{
		printf("Invalid number of resolvers or requesters inputted. Please double check your arguments.\n");
		return -1;
	}

	printf("Processing %d files with %d requesters, %d, resolvers, with files:\n", num_files, num_requesters, num_resolvers);

	for(int i = 0; i < num_files; i++)
	{
		printf("%s\n", input_file_names[i]);
	}

	printf("Output will be place in %s for requesters, and %s for resolvers\n", requester_log_name, resolver_log_name);

	/**
	* Input Gathering ends here
	*/

	// Next goal: multiple requester_threads reading the files and writing it
	requester_threads = (pthread_t *)malloc(sizeof(pthread_t) * num_requesters);
	requester_params_list = init_requester_info(num_requesters, num_files, files, requester_threads);

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_create(&requester_threads[i], NULL, requester, &requester_params_list[i]);
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_join(requester_threads[i], NULL);
	}

	printf("joined!");

	free(requester_params_list);
	free(requester_threads);
	close_files(num_files, files);
	free(files);
	free(input_file_names);

	return 0;
}