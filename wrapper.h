#ifndef WRAPPER_H
#define WRAPPER_H

#include <mpi.h>
#include <pthread.h>
#include <time.h>
#include <openssl/sha.h>

//#define _GNU_SOURCE
#include "orderedhashmap.h"

#define PRINT_RANK 4
#define DEBUG_COMP_PRINT 0
#define DEBUG_UFFD_PRINT 0
#define DEBUG_ISEND_PRINT 0
#define DEBUG_REG_PRINT 0

#define USLEEPTIME 1000
#define ISEND_SLEEP 1

extern LRUCache *cache;
extern pthread_mutex_t cache_lock;
extern pthread_mutex_t reg_lock;

extern int last_comp_index;

extern MPI_Request ***requests;


// compression thread structure
typedef struct comp_thread_args {
	int tn;
	int total;
	int rank;
} comp_thread_args;


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

	int ncomp;
	int sending;

	int comp_size;
	int ready;
	unsigned long request;

	double comp_time;
	double send_time;
	double last_fault;

	int thread;
	int faults;

	char *aligned_addr;
	size_t aligned_size;

	pthread_mutex_t pair_lock;
} Pair;

typedef struct register_addr {
	char *region;
	int size;
	int atomic;

	int dirty;

	pthread_mutex_t reg_lock;
} reg_addr;

typedef struct register_addr_list {
	reg_addr *list;
	int size;
	int pos;
} reg_addr_list;

typedef struct fault_list {
	struct fault_handler_args *args;
	int size;
	int pos;
} fault_list;

extern struct fault_handler_args *fargs;

extern reg_addr_list *reg_list;
extern fault_list    flist;

extern Pair *pair;
extern int pair_size;
extern pthread_mutex_t creation_lock;
extern pthread_mutex_t reg_lock;

extern int reg_first;

#define INITIAL_CAPACITY 26

// Structure to manage a dynamic list of receive addresses and requests
typedef struct {
    char **recv_addrs;    // Array of receiving addresses
    unsigned long *requests; // Array of MPI requests
    size_t *recv_size;         // Array of receiving sizes
    int *tag;
    int size;              // Current number of requests
    int capacity;          // Max capacity of the list
} recv_manager_t;

void recv_manager_init(recv_manager_t *manager);
void recv_manager_add(recv_manager_t *manager, void *recv_addr, size_t size,
		int tag, unsigned long request);
void recv_manager_free(recv_manager_t *manager);

void init_fault_list();
void add_fault_args(int uffd, size_t length, void *addr, int rank);

reg_addr_list *init_register_list();
reg_addr_list *realloc_register_list();
int add_reg_pair(char *region, int size);


void uffd_register(char *addr, size_t size);
void *handler(void *arg);

extern recv_manager_t* manager;

// MPI Wrapper
int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided );
int wrapper_MPI_Init( int *argc, char ***argv );
int wrapper_MPI_Isend( void *buf, int count, MPI_Datatype type, int dest,
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
void try_decompress( char *input_buffer, int input_size, size_t supposed_recv_size );


// core allocator
int core_allocator();

int find_and_create( char *addr, int size );
void hint_free_starts(char *ptr);
void *starts_async_compression(void *arg);

#endif
