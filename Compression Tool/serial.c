/*
Description: This program compresses frames of a video using
		the zlib library in multiple threads.

Authors: James Yab, Carlos Irizarry, Isabelle Troya
*/

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#include <pthread.h>

#define BUFFER_SIZE 1048576 // 1MB
#define MAX_THREADS 19

typedef struct thread_args
{
	const int *nfiles;
	const char *folder_name;
	const char **files;
	const int *tid;
	const FILE *f_out;

	// atomic
	int *total_in;
	int *total_out;
	pthread_mutex_t *lock_total_in;
	pthread_mutex_t *lock_total_out;

	unsigned char *buffer_in;
	unsigned char *buffer_out;

	unsigned char **shared_compressed_files;
} thread_args;

int cmp(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

// routine for every thread
void *compress_frame(void *thread_args)
{

	// type casting all arguments
	struct thread_args *args = (struct thread_args *)thread_args;
	const int *nfiles = (const int *)args->nfiles;
	const char *folder_name = (const char *)args->folder_name;
	const char **files = (const char **)args->files;
	const int *tid = (const int *)args->tid;
	FILE *f_out = (FILE *)args->f_out;

	// Atomic variables casting
	int *total_in = (int *)args->total_in;
	int *total_out = (int *)args->total_out;
	pthread_mutex_t *lock_total_in = (pthread_mutex_t *)args->lock_total_in;
	pthread_mutex_t *lock_total_out = (pthread_mutex_t *)args->lock_total_out;

	unsigned char *buffer_in = (unsigned char *)args->buffer_in;
	unsigned char *buffer_out = (unsigned char *)args->buffer_out;

	// shared buffer to place in order
	unsigned char **shared_compressed_files =
		(unsigned char **)args->shared_compressed_files;

	// thread compress for every MAX_THREADS-th file
	for (int i = *tid; i < *nfiles; i += MAX_THREADS)
	{

		// create full path
		int len = strlen(folder_name) + strlen(files[i]) + 2; // +2 for '/' and '\0'
		char *full_path = malloc(len * sizeof(char));
		assert(full_path != NULL);
		strcpy(full_path, folder_name);
		strcat(full_path, "/");
		strcat(full_path, files[i]);

		// read from file
		FILE *f_in = fopen(full_path, "r");
		assert(f_in != NULL);
		int nbytes = fread(buffer_in, sizeof(unsigned char), BUFFER_SIZE, f_in);
		fclose(f_in);

		free(full_path);

		// update the total number of bytes read atomically
		pthread_mutex_lock(lock_total_in);
		*total_in += nbytes;
		pthread_mutex_unlock(lock_total_in);

		// zip file
		z_stream
			strm; // strm is the structure that holds the state of the compressor

		// initialize the compression state
		int ret = deflateInit(&strm, 9);
		assert(ret == Z_OK);
		strm.avail_in = nbytes;		  // sets the number of bytes to be compressed
		strm.next_in = buffer_in;	  // sets the input data that will be compressed
		strm.avail_out = BUFFER_SIZE; // sets the size of the output buffer
		strm.next_out =
			buffer_out; // sets output buffer where compressed data will be written

		ret = deflate(&strm, Z_FINISH); // performs the compression
		assert(ret == Z_STREAM_END);	// check if the compression was successful

		// dump zipped file
		int nbytes_zipped =
			BUFFER_SIZE - strm.avail_out; // number of bytes compressed

		// store the compressed file in the shared buffer
		shared_compressed_files[i] = malloc(sizeof(int) + nbytes_zipped);
		assert(shared_compressed_files[i] != NULL);
		memcpy(shared_compressed_files[i], &nbytes_zipped, sizeof(int));
		memcpy(shared_compressed_files[i] + sizeof(int), buffer_out, nbytes_zipped);

		// update the total number of bytes compressed atomically
		pthread_mutex_lock(lock_total_out);
		*total_out += nbytes_zipped;
		pthread_mutex_unlock(lock_total_out);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	// time computation header
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	// end of time computation header

	// do not modify the main function before this point!

	assert(argc == 2);

	DIR *d;
	struct dirent *dir;
	char *folder_name = argv[1];
	char **files = NULL;
	int nfiles = 0;

	// Create an array to store threads
	pthread_t tid[MAX_THREADS];

	d = opendir(folder_name);
	if (d == NULL)
	{
		printf("An error has occurred\n");
		return 0;
	}

	// create sorted list of PPM files
	while ((dir = readdir(d)) != NULL)
	{
		files = realloc(files, (nfiles + 1) * sizeof(char *));
		assert(files != NULL);

		int len = strlen(dir->d_name);
		if (dir->d_name[len - 4] == '.' && dir->d_name[len - 3] == 'p' &&
			dir->d_name[len - 2] == 'p' && dir->d_name[len - 1] == 'm')
		{
			files[nfiles] = strdup(dir->d_name);
			assert(files[nfiles] != NULL);

			nfiles++;
		}
	}
	closedir(d);
	qsort(files, nfiles, sizeof(char *), cmp);

	// create a single zipped package with all PPM files in lexicographical order
	int total_in = 0, total_out = 0;
	FILE *f_out = fopen("video.vzip", "w");
	assert(f_out != NULL); // if f_out is NULL, means the file could not be
						   // created and then terminates program

	// create a thread for each file
	int temp_thread_indices[MAX_THREADS];

	// array of thread struct arguments
	thread_args args[MAX_THREADS];
	pthread_mutex_t lock_total_in = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_t lock_total_out = PTHREAD_MUTEX_INITIALIZER;

	// SHARED BUFFER
	unsigned char **compressed_data = malloc(nfiles * sizeof(unsigned char *));
	assert(compressed_data != NULL);

	// CREATE THREADS
	for (int i = 0; i < MAX_THREADS; i++)
	{

		// stores the index of the thread
		temp_thread_indices[i] = i;

		// initialize thread arguments
		args[i].nfiles = &nfiles;
		args[i].folder_name = folder_name;
		args[i].files = (const char **)files;
		args[i].tid = &temp_thread_indices[i];
		args[i].f_out = f_out;

		// initialize atomic variables and lock
		args[i].total_in = &total_in;
		args[i].total_out = &total_out;
		args[i].lock_total_in = &lock_total_in;
		args[i].lock_total_out = &lock_total_out;

		// initialize buffers
		args[i].buffer_in = malloc(BUFFER_SIZE * sizeof(unsigned char));
		assert(args[i].buffer_in != NULL);

		args[i].buffer_out = malloc(BUFFER_SIZE * sizeof(unsigned char));
		assert(args[i].buffer_out != NULL);

		args[i].shared_compressed_files = compressed_data;

		// give arguments to the thread routine
		pthread_create(&tid[i], NULL, compress_frame, &args[i]);
	}

	// wait for all threads to finish
	for (int i = 0; i < MAX_THREADS; i++)
	{
		pthread_join(tid[i], NULL);
	}

	// write the buffer to the final zip
	for (int i = 0; i < nfiles; i++)
	{
		fwrite(compressed_data[i], sizeof(int), 1, f_out);
		fwrite(compressed_data[i] + sizeof(int), sizeof(unsigned char), *((int *)compressed_data[i]), f_out);
		free(compressed_data[i]);
	}
	free(compressed_data);

	// free all buffers
	for (int i = 0; i < MAX_THREADS; i++)
	{
		free(args[i].buffer_in);
		free(args[i].buffer_out);
	}

	fclose(f_out);

	printf("Compression rate: %.2lf%%\n", 100.0 * (total_in - total_out) / total_in);

	// release list of files
	for (int i = 0; i < nfiles; i++)
		free(files[i]);
	free(files);

	// do not modify the main function after this point!

	// time computation footer
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("Time: %.2f seconds\n", ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) - ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
	// end of time computation footer

	return 0;
}
