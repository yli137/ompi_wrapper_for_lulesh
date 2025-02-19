#include "wrapper.h"

#include <mpi.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <pthread.h>
#include <sched.h>
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


recv_manager_t* manager = NULL;
int recv_count = 0;
int wait_count = 0;

reg_addr_list *reg_list = NULL;
struct fault_handler_args *fargs = NULL;

int wrapper_MPI_Isend( void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int index = find_and_create((char*)buf, type_size);

	if(index == -1)
		index = find_and_create((char*)buf, type_size);

	pair[index].comp_size = compress_lz4_buffer(pair[index].isend_addr, 
			pair[index].isend_size,
			pair[index].comp_addr,
			pair[index].comp_size);
	if(pair[index].comp_size < type_size && pair[index].comp_size != 0){
		//write_data_to_file(buf, type_size, rank, dest, tag, type_size);
		printf("%d %d %d\n", rank, pair[index].comp_size, type_size);
		return MPI_Send(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
				dest, tag, comm);
		//return MPI_Isend(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
		//		dest, tag, comm, request);
	}

	printf("%d %d %d\n", rank, type_size, type_size);
	return MPI_Send( buf, count, type, dest, tag, comm );
	//return MPI_Isend( buf, count, type, dest, tag, comm, request );
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int size;
	MPI_Type_size(type, &size);
	size *= count;

	if(manager == NULL){
		manager = (recv_manager_t*)malloc(sizeof(recv_manager_t));
		recv_manager_init(manager);
	}

	recv_manager_add(manager, buf, tag, source, size);

	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status)
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int ret = MPI_Wait(request, status);
	int count;
	MPI_Get_count(status, MPI_BYTE, &count);

	for(int i = 0; i < manager->size; i++){
		if(manager->recv_addrs[i] == NULL)
			continue;

		if( status->MPI_SOURCE == manager->source[i] && status->MPI_TAG == manager->tag[i] ){
			if(count != manager->recv_size[i]){
				try_decompress(manager->recv_addrs[i], count, manager->recv_size[i]);
				//read_and_compare(manager->recv_addrs[i], status->MPI_SOURCE, rank, status->MPI_TAG, manager->recv_size[i]);
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
	return MPI_Waitall(count, array_of_requests, array_of_statuses);
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	return MPI_Init_thread( argc, argv, required, provided );
}
