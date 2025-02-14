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

recv_manager_t* manager = NULL;
int recv_count = 0;
int wait_count = 0;

reg_addr_list *reg_list = NULL;
struct fault_handler_args *fargs = NULL;

int wrapper_MPI_Isend( void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	//int *change = (int*)buf;
	//(*change)++;

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

#if DEBUG_ISEND_PRINT
	if(rank == PRINT_RANK)
		printf("Starts isend %p\n", buf);
#endif

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int size1 = 5000; //2160000; //960000; //240000; //960000; //240000;
	int index = -1;

#if DEBUG_ISEND_PRINT
	if(rank == PRINT_RANK){
		for(int i = 0; i < pair_size; i++)
			printf("Isend pair %d pair_size %d ready %d\n", i, pair_size, pair[i].ready);
		for(int i = 0; i < reg_list->pos; i++)
			printf("Isend reg %d reg_size %d dirty %d\n", i, reg_list->pos, reg_list->list[i].dirty);
	}
#endif

	if(type_size >= size1){
		index = find_and_create((char*)buf, type_size);

		if(index == -1){
			index = find_and_create((char*)buf, type_size);
			uffd_register((char*)buf, type_size);
			pair[index].request = (unsigned long)request;

		} else if(index != -1){
			usleep(ISEND_SLEEP);
			if(pthread_mutex_trylock(&(pair[index].pair_lock)) == 0){
				pair[index].ncomp = 0;

				if(pair[index].ready == 1 && pair[index].comp_size < pair[index].isend_size){

					//printf("%d %d %d\n", rank, pair[index].comp_size, type_size);
					int comp_ret = MPI_Isend(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
							dest, tag, comm, request);
					pair[index].sending = 1;
					pair[index].faults = 0;
					pair[index].request = (unsigned long)request;


					pthread_mutex_unlock(&(pair[index].pair_lock));
					return comp_ret;
				}

				pthread_mutex_unlock(&(pair[index].pair_lock));
			}
		}
	}

	//printf("%d %d %d\n", rank, type_size, type_size);
	int ret = MPI_Isend( buf, count, type, dest, tag, comm, request );
	return ret;
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	if(manager == NULL){
		manager = (recv_manager_t*)malloc(sizeof(recv_manager_t));
		recv_manager_init(manager);
	}

	int size;
	MPI_Type_size(type, &size);
	size *= count;

	recv_manager_add(manager, buf, size, tag, (unsigned long)request);

	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status)
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int ret = MPI_Wait(request, status);
	int tag = status->MPI_TAG;
	int count;
	MPI_Get_count(status, MPI_BYTE, &count);

	for(int i = 0; i < pair_size; i++){
		if(pair[i].request != 0){
			if(pair[i].request == (unsigned long)request){
				pthread_mutex_lock(&(pair[i].pair_lock));

				pair[i].sending = 0;
				pair[i].request = 0;
				pthread_mutex_unlock(&(pair[i].pair_lock));
			}
		}
	}

	for(int i = 0; i < manager->size; i++){
		if(manager->recv_addrs[i] == NULL)
			continue;

		if( ((unsigned long)request == manager->requests[i]) && (tag == manager->tag[i])){

			try_decompress(manager->recv_addrs[i], count, manager->recv_size[i]);
			//manager->recv_addrs[i] = NULL;
			manager->tag[i] = -1;

			int j = 0;
			for(; j < manager->size; j++)
				if(manager->tag[j] != -1)
					break;

			if(j == manager->size)
				manager->size = 0;

			return ret;
		}
	}

	return ret;
}

int wrapper_MPI_Waitall( int count, MPI_Request array_of_requests[],
		MPI_Status *array_of_statuses )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int ret = MPI_Waitall(count, array_of_requests, array_of_statuses);

	for(int j = 0; j < count; j++){
		for(int i = 0; i < pair_size; i++){
			if(pair[i].request != 0){
				if(pair[i].request == (unsigned long)(array_of_requests[j])){
					pthread_mutex_lock(&(pair[i].pair_lock));

					pair[i].sending = 0;
					pair[i].request = 0;
					pair[i].comp_size = pair[i].isend_size+100;
					pthread_mutex_unlock(&(pair[i].pair_lock));
				}
			}
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
	pthread_mutex_lock(&cache_lock);
	cache = create_cache(100);
	pthread_mutex_unlock(&cache_lock);
	reg_list = init_register_list();
	init_fault_list();

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
