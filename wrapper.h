#ifndef WRAPPER_H
#define WRAPPER_H

#include <mpi.h>
#include <pthread.h>
#include <time.h>

extern int first_encounter;

typedef struct addr_pair {
	char *isend_addr;
	int isend_size;
	char *comp_addr;
	int comp_size;
	int ready;
	int sending;

	struct timespec ts;

	pthread_mutex_t pair_lock;
} Pair;
extern Pair *pair;
extern int pair_size;

extern pthread_mutex_t creation_lock;

#define INITIAL_CAPACITY 26

// Structure to manage a dynamic list of receive addresses and requests
typedef struct {
    char **recv_addrs;    // Array of receiving addresses
    MPI_Request **requests; // Array of MPI requests
    int *tag;
    int size;              // Current number of requests
    int capacity;          // Max capacity of the list
} recv_manager_t;

void recv_manager_init(recv_manager_t *manager);
void recv_manager_add(recv_manager_t *manager, void *recv_addr, int tag, MPI_Request *request);
void recv_manager_free(recv_manager_t *manager);


extern recv_manager_t* manager;

// MPI Wrapper
int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided );
int wrapper_MPI_Isend( const void *buf, int count, MPI_Datatype type, int dest,
		       int tag, MPI_Comm comm, MPI_Request *request );
int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
        	       int tag, MPI_Comm comm, MPI_Request *request );

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status);
int wrapper_MPI_Waitall( int count, MPI_Request array_of_requests[],
	                 MPI_Status *array_of_statuses );

// Compression/Decompression
int compress_lz4_buffer( const char *input_buffer, int input_size,
		         char *output_buffer, int output_size );
int decompress_lz4_buffer_default( const char *input_buffer, int input_size,
		                   char *output_buffer, int output_size );
void try_decompress( char *input_buffer, int input_size );


// core allocator
int core_allocator();

int find_and_create( char *addr, int size );
void hint_compression_starts();
void hint_compression_starts7_13();
void hint_compression_starts14_16();

void hint_free_starts(char *ptr);

void *starts_async_compression(void *arg);

#endif
