#include "multi-lookup.h"

#define MAX_LENGTH 1025

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
	int all_files_serviced;
	int read_count;
	FILE * resolver_log;
};

/*
* Read and write to buffer are structured very similarly to the pgm4.c
* code of the consumer producer solution
*/
int write_to_buffer(struct requester_info * info)
{
	struct buffer_sync * sync = info->sync;
	pthread_mutex_lock(sync->mutex);
	while(sync->buffer_count == sync->buffer_size)
	{
		pthread_cond_wait(sync->condp, sync->mutex);
	}

	/*
	* Attempt to write to the buffer
	*/
	size_t len = 0;
	sync->buffer[sync->buffer_count] = "";
	if(getline(&sync->buffer[sync->buffer_count], &len, info->files[info->file_num]) == -1)
	{
		free(sync->buffer[sync->buffer_count]);
		pthread_mutex_unlock(sync->mutex);
		return 0;
	}

	//printf("At buffer pos %d, thread %ld wrote %s", sync->buffer_count, syscall(SYS_gettid), sync->buffer[sync->buffer_count]);
	sync->buffer_count++;

	pthread_cond_signal(sync->condc);
	pthread_mutex_unlock(sync->mutex);

	return 1;
}

char * read_from_buffer(struct resolver_info * info)
{
	char * line = "Yodobashi camera";
	struct buffer_sync * sync = info->sync;

	pthread_mutex_lock(sync->mutex);
	while(sync->buffer_count == 0)
	{
		pthread_cond_wait(sync->condc, sync->mutex);

		// There is nothing more to service.
		if(info->all_files_serviced)
		{
			pthread_mutex_unlock(sync->mutex);
			return "";
		}
	}

	sync->buffer_count--;
	line = sync->buffer[sync->buffer_count];
	//printf("At buffer pos %d, thread %ld read %s", sync->buffer_count, syscall(SYS_gettid), line);

	pthread_cond_signal(sync->condp);
	pthread_mutex_unlock(sync->mutex);

	return line;
}

void * requester(void * ptr)
{
	struct requester_info * info = ((struct requester_info *)ptr);
	int files_serviced = 0;
	int total_lines_serviced = 0;

	for(int i = 0; i < info->num_files; i++)
	{
		int lines_serviced = 0; // Keeps track if the thread has serviced at least one line of a  file

		while(write_to_buffer(info))
		{
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

	pthread_mutex_lock(info->serviced_write_lock);
	fprintf(info->requester_log, "Thread %ld serviced %d files and %d lines \n", syscall(SYS_gettid), files_serviced, total_lines_serviced);
	pthread_mutex_unlock(info->serviced_write_lock);

	pthread_exit(0);
}

void * resolver(void * ptr)
{
	struct resolver_info * info = ((struct resolver_info *) ptr);
	while(!info->all_files_serviced || info->sync->buffer_count > 0)
	{
		char * line = read_from_buffer(info);

		/*
		* Check the length of the string
		*/
		int len = strnlen(line, MAX_LENGTH);
		if(len == MAX_LENGTH)
		{
			printf("Skipping, host name %s is longer than 1025 characters.", line);
			continue;
		}

		/*
		* Remove newline characters
		*/
		if(line[len - 1] == '\n')
		{
			line[len - 1] = '\0';
		}

		char * ip = malloc(sizeof(*ip) * MAX_LENGTH);
		strncpy(ip, "", MAX_LENGTH);
		if(dnslookup(line, ip, MAX_LENGTH) == UTIL_FAILURE)
		{
			printf("Warning: host %s failed to connect\n", line);
		}

		pthread_mutex_lock(info->results_write_lock);
		fprintf(info->resolver_log, "Thread %ld read %s with dnslookup %s\n", syscall(SYS_gettid), line, ip);
		free(line);
		free(ip);
		pthread_mutex_unlock(info->results_write_lock);
	}

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
	FILE * resolver_log;
	int num_files;
	const char ** input_file_names; 
	FILE ** files; // The files that are opened for the duration of the program
	pthread_t * requester_threads; // The requester_threads
	pthread_t * resolver_threads;
	struct requester_info * requester_params_list; // The requester parameters
	pthread_mutex_t serviced_write_lock; // Enforces mutual exclusion on writing to "serviced.txt"
	int buffer_size = 8;
	char ** buffer;
	struct buffer_sync * sync;
	struct resolver_info * resolver_params;
	pthread_mutex_t results_write_lock;

	/*
	* All of these are for the bounded buffer problem + reader-writer problem
	*/
	pthread_mutex_t mutex;
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
	* Open and validate logs
	*/
	requester_log = fopen(requester_log_name, "w");
	if(requester_log == NULL)
	{
		printf("Invalid requester log name\n");
		return -1;
	}

	resolver_log = fopen(resolver_log_name, "w");
	if(resolver_log == NULL)
	{
		printf("Invalid resolver log name\n");
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
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&condc, NULL);
	pthread_cond_init(&condp, NULL);
	pthread_mutex_init(&results_write_lock, NULL);

	requester_threads = malloc(sizeof(pthread_t) * num_requesters);
	resolver_threads = malloc(sizeof(pthread_t) * num_resolvers);
	requester_params_list = (struct requester_info *)malloc(sizeof(* requester_params_list) * num_requesters);
	resolver_params = malloc(sizeof(* resolver_params));
	buffer = malloc((sizeof(*buffer)) * buffer_size);
	sync = malloc(sizeof(*sync));

	sync->mutex = &mutex;
	sync->condc = &condc;
	sync->condp = &condp;
	sync->buffer = buffer;
	sync->buffer_count = 0;
	sync->buffer_size = buffer_size;

	/*
	* Initialize parameters for the requester threads
	*/
	for(int i = 0; i < num_requesters; i++)
	{
		requester_params_list[i].files = files;
		requester_params_list[i].file_num = i % num_files;
		requester_params_list[i].num_files = num_files;
		requester_params_list[i].thread = &requester_threads[i];
		requester_params_list[i].serviced_write_lock = &serviced_write_lock;
		requester_params_list[i].requester_log = requester_log;
		requester_params_list[i].sync = sync;
	}

	/*
	* Initialize parameters for resolver threads
	*/

	resolver_params->sync = sync;
	resolver_params->results_write_lock = &results_write_lock;
	resolver_params->all_files_serviced = 0;
	resolver_params->read_count = 0;
	resolver_params->resolver_log = resolver_log;

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_create(&requester_threads[i], NULL, requester, &requester_params_list[i]);
	}

	for(int i = 0; i < num_resolvers; i++)
	{
		pthread_create(&resolver_threads[i], NULL, resolver, resolver_params);
	}

	for(int i = 0; i < num_requesters; i++)
	{
		pthread_join(requester_threads[i], NULL);
	}

	// Once all the requester threads have joined together, 
	// all files have been serviced from the requester side
	pthread_mutex_lock(sync->mutex);
	resolver_params->all_files_serviced = 1;
	pthread_cond_broadcast(sync->condc);
	pthread_mutex_unlock(sync->mutex);

	for(int i = 0; i < num_resolvers; i++)
	{
		pthread_join(resolver_threads[i], NULL);
	}

	printf("joined!\n");

	free(sync);
	free(buffer);
	free(resolver_params);
	free(requester_params_list);
	free(resolver_threads);
	free(requester_threads);
	pthread_mutex_destroy(&results_write_lock);
	pthread_mutex_destroy(&serviced_write_lock);
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&condc);
	pthread_cond_destroy(&condp);
	fclose(resolver_log);
	fclose(requester_log);
	close_files(num_files, files);
	free(files);
	free(input_file_names);

	return 0;
}