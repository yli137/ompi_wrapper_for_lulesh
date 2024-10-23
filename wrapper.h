#ifndef WRAPPER_H
#define WRAPPER_H

#include <mpi.h>
#include <pthread.h>
#include <time.h>
#include <openssl/sha.h>

extern int first_encounter;

// Structure to pass to the fault handler thread
struct fault_handler_args {
    int uffd;
    size_t length;
    void* address;
    int rank;
};

struct params {
    int uffd;
    long page_size;
};

typedef struct addr_pair {
	char *isend_addr;
	int isend_size;
	char *comp_addr;
	int comp_size;
	int created;

	pthread_mutex_t pair_lock;
} Pair;

typedef struct register_addr {
	char *region;
	int size;
} reg_addr;

typedef struct register_addr_list {
	reg_addr *list;
	int size;
	int pos;
} reg_addr_list;

extern reg_addr_list *reg_list;

extern struct fault_handler_args *args;

extern Pair *pair;
extern int pair_size;
extern pthread_mutex_t creation_lock;

extern int reg_first;

extern unsigned char hash[SHA256_DIGEST_LENGTH];
extern unsigned char hashprev[SHA256_DIGEST_LENGTH];

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


reg_addr_list *init_register_list();
reg_addr_list *realloc_register_list();
void add_reg_pair(char *region, int size);


void uffd_register(char *addr, size_t size, int rank);

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
void hint_free_starts(char *ptr);
void *starts_async_compression(void *arg);

#endif
