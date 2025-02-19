#include "wrapper.h"
#include "orderedhashmap.h"

#include <mpi.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <pthread.h>

#include <sched.h>
#include <hwloc.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>


#include <linux/userfaultfd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>



LRUCache *cache;
pthread_mutex_t cache_lock;
pthread_mutex_t reg_lock;
pthread_mutex_t creation_lock;

double total_reg = 0;

recv_manager_t* manager = NULL;
int recv_count = 0;
int wait_count = 0;

reg_addr_list *reg_list = NULL;
struct fault_handler_args *fargs = NULL;


void write_data_to_file(void *data, size_t size, int source, int dest, int tag, int data_size) {
    // Generate filename dynamically
    char filename[100];
    snprintf(filename, sizeof(filename), "/home/yli137/lulesh/wrapper-lulesh/comp_data/%d_%d_%d_%d.bin", source, dest, tag, data_size);

    // Open file for binary writing
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file");
        return;
    }

    // Write data to file
    size_t written = fwrite(data, 1, size, file);
    if (written != size) {
        fprintf(stderr, "Error writing data to file: %s\n", filename);
    }

    // Close file
    fclose(file);
};
void read_and_compare(const void *compare_buffer, int source, int dest, int tag, int data_size)
{
	char filename[100];
	snprintf(filename, sizeof(filename), "/home/yli137/lulesh/wrapper-lulesh/comp_data/%d_%d_%d_%d.bin", source, dest, tag, data_size);

	// Open file for binary reading
	FILE *file = fopen(filename, "rb");
	if (!file) {
		perror("Error opening file for reading");
		return;
	}

	// Allocate buffer to store file data
	void *file_buffer = malloc(data_size);
	if (!file_buffer) {
		fprintf(stderr, "Memory allocation failed\n");
		fclose(file);
		return;
	}

	// Read data from file
	size_t read_size = fread(file_buffer, 1, data_size, file);
	fclose(file);

	if ((int)read_size != data_size) {
		fprintf(stderr, "Error reading file: expected %d bytes, got %zu bytes\n", data_size, read_size);
		free(file_buffer);
		return;
	}

	// Compare file buffer with provided buffer
	if (memcmp(file_buffer, compare_buffer, data_size) == 0) {
		printf("Comparison successful: No differences found.\n");
	} else {
		printf("Comparison failed: Data mismatch found. source %d dest %d tag %d data_size %d\n",
				source, dest, tag, data_size);
	}

	// Free allocated memory
	free(file_buffer);

	remove(filename);
}


int wrapper_MPI_Isend( void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int size1 = 1000; //2160000; //960000; //240000; //960000; //240000;
	int index = -1;

	if(type_size >= size1){
		index = find_and_create((char*)buf, type_size);

		if(index == -1){
			index = find_and_create((char*)buf, type_size);
			//pthread_mutex_lock(&cache_lock);
			//put(cache, (unsigned long)(pair[index].isend_addr) % (size_t)(pair[index].isend_size), (size_t)(pair[index].isend_size));
			//pthread_mutex_unlock(&cache_lock);
			
			uffd_register((char*)buf, type_size);
			pair[index].request = (unsigned long)request;

			pair[index].last_time = get_timestamp();

		} else if(index != -1){
			if(pthread_mutex_trylock(&(pair[index].pair_lock)) == 0){
				pair[index].ncomp = 0;
				
				if(pair[index].ready == 1 && pair[index].comp_size < pair[index].isend_size && pair[index].comp_size > 0){
					printf("send to %d %d %d source %d tag %d faults %d\n", dest, pair[index].comp_size, type_size,
							rank, tag, pair[index].faults);
					//int comp_ret = MPI_Isend(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
					//		dest, tag, comm, request);

					write_data_to_file(buf, type_size, rank, dest, tag, type_size);
					int comp_ret = MPI_Send(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE, dest, tag, comm);
					//int comp_ret = MPI_Send(buf, count, type, dest, tag, comm);
					//pair[index].sending = 1;
					pair[index].faults = 0;
					pair[index].request = (unsigned long)request;

					pthread_mutex_unlock(&(pair[index].pair_lock));
					return comp_ret;
				}

				pair[index].faults = 0;
				pthread_mutex_unlock(&(pair[index].pair_lock));
			}
		}
		//pair[index].sending = 2;
	}

	//printf("%d %d %d\n", rank, type_size, type_size);
	//return MPI_Isend( buf, count, type, dest, tag, comm, request );
	return MPI_Send(buf, count, type, dest, tag, comm);
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	if(manager == NULL){
		manager = (recv_manager_t*)malloc(sizeof(recv_manager_t));
		recv_manager_init(manager);
	}

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int size;
	MPI_Type_size(type, &size);
	size *= count;

	recv_manager_add(manager, buf, size, source, tag);

	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status)
{
	int ret = MPI_Wait(request, status);
	int count;
	MPI_Get_count(status, MPI_BYTE, &count);

	int rank = 0;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	for(int i = 0; i < manager->size; i++){
		if(manager->recv_addrs[i] == NULL)
			continue;

		if( status->MPI_SOURCE == manager->source[i] && status->MPI_TAG == manager->tag[i] ){

			if(count < manager->recv_size[i]){
				try_decompress(manager->recv_addrs[i], count, manager->recv_size[i]);
				read_and_compare(manager->recv_addrs[i], status->MPI_SOURCE, rank, status->MPI_TAG, manager->recv_size[i]);
			}
			manager->recv_addrs[i] = NULL;
			manager->tag[i] = -1;

			return ret;
		}
	}

	return ret;
}

int wrapper_MPI_Waitall( int count, MPI_Request array_of_requests[],
		MPI_Status *array_of_statuses )
{
	int ret = MPI_Waitall(count, array_of_requests, array_of_statuses);

	for(int j = 0; j < count; j++){
		for(int i = 0; i < pair_size; i++){
			pthread_mutex_lock(&(pair[i].pair_lock));
			if(pair[i].request != 0){
				if(pair[i].request == (unsigned long)(array_of_requests[j])){
					pair[i].sending = 0;
					pair[i].request = 0;
				}
			}
			pthread_mutex_unlock(&(pair[i].pair_lock));
		}
	}

	return ret;
}

int wrapper_MPI_Init( int *argc, char ***argv )
{
	int ret = MPI_Init( argc, argv );

	return ret;
}


int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );

	// init LRU cache and register list and pair list
	// registration lock
	if(pthread_mutex_init(&reg_lock, NULL) != 0){
		perror("registration lock initialization failed\n");
	}
	if(pthread_mutex_init(&cache_lock, NULL) != 0){
		perror("cache lock initialization failed\n");
	}
	if(pthread_mutex_init(&creation_lock, NULL) != 0){
		perror("creation lock initialization failed\n");
	}
	if(pthread_mutex_init(&uffd_lock, NULL) != 0){
		perror("creation lock initialization failed\n");
	}

	pthread_mutex_lock(&cache_lock);
	cache = create_cache(100);
	pthread_mutex_unlock(&cache_lock);
	reg_list = init_register_list();
	init_fault_list();

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	fargs = (struct fault_handler_args*)malloc(sizeof(struct fault_handler_args));
	fargs->uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	fargs->rank = rank;

	struct uffdio_api uffdio_api;
	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
	assert(ioctl(fargs->uffd, UFFDIO_API, &uffdio_api) != -1);

	pthread_t uffd_thread;
	assert(pthread_create(&uffd_thread, NULL, handler, (void*)fargs) == 0);

	pthread_t compression_thread1;//, compression_thread2;
	comp_thread_args *arg1 = (comp_thread_args*)malloc(sizeof(comp_thread_args));
	arg1->tn = 0;
	arg1->total = 2;
	arg1->rank = rank;

	assert(pthread_create(&compression_thread1, NULL, starts_async_compression, (void*)arg1) == 0);

	return ret;
}
