#include "multi-lookup.h"
#include "util.h"
#include <stdio.h>
#include <pthread.h>

struct requester_info
{
	const char ** input_file_names;
	int file_num;
};

void * requester(void * ptr)
{
	printf("Ima thread.");
	pthread_exit(0);
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

	const char ** input_file_names = (const char **)malloc(sizeof(char *) * num_files);

	for(int i = 5; i < argc; i++)
	{
		input_file_names[i - 5] = argv[i];
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

	// Next goal: multiple requester threads reading the files and writing it
	pthread_t * threads = (pthread_t *)malloc(sizeof(pthread_t) * num_requesters);

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_create(&threads[i], NULL, requester, NULL);
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_join(threads[i], NULL);
	}

	printf("joined!");

	free(threads);
	free(input_file_names);

	return 0;
}