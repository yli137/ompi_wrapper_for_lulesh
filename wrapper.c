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


recv_manager_t* manager = NULL;
int recv_count = 0;
int wait_count = 0;

reg_addr_list *reg_list = NULL;

int reg_first = 0;
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
	if(index == -1 && type_size > 9000 && rank == 0){
		
		index = find_and_create((char*)buf, type_size);

		uffd_register((char*)buf, type_size, reg_first, reg_first);
		pair[index].comp_size = compress_lz4_buffer(pair[index].isend_addr, 
				pair[index].isend_size,
				pair[index].comp_addr,
				pair[index].comp_size);
	}

#if 0
	else if(rank == 0 && index != -1){// && type_size > 9000){
		if(pair[index].comp_size < type_size){
			pthread_mutex_lock(&(pair[index].pair_lock));
			//printf("send index %d comp_size %d type_size %d\n",
			//		index, pair[index].comp_size, type_size);
			return MPI_Isend(pair[index].comp_addr, pair[index].comp_size, MPI_BYTE,
					dest, tag, comm, request);
		}
	}
#endif

	reg_first++;

	return MPI_Isend( buf, count, type, dest, tag, comm, request );
}

int wrapper_MPI_Irecv( void *buf, int count, MPI_Datatype type, int source,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	if(manager == NULL){
		manager = (recv_manager_t*)malloc(sizeof(recv_manager_t));
		recv_manager_init(manager);
	}

	recv_manager_add(manager, buf, tag, request);

	return MPI_Irecv( buf, count, type, source, tag, comm, request );
}

int wrapper_MPI_Wait(MPI_Request *request, MPI_Status *status)
{
	int ret = MPI_Wait(request, status);
	int tag = status->MPI_TAG;
	int count;
	MPI_Get_count(status, MPI_BYTE, &count);

	for(int i = 0; i < manager->size; i++){
		if(manager->recv_addrs[i] == NULL)
			continue;

		if( ((uintptr_t)request == (uintptr_t)(manager->requests[i])) && (tag == manager->tag[i])){
			try_decompress(manager->recv_addrs[i], count);
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
	for(int i = 0; i < pair_size; i++)
		pthread_mutex_unlock(&(pair[i].pair_lock));
	return MPI_Waitall(count, array_of_requests, array_of_statuses);
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );
	reg_list = init_register_list();
	init_fault_list();

	fargs = (struct fault_handler_args*)malloc(sizeof(struct fault_handler_args));
	fargs->uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

	struct uffdio_api uffdio_api;
	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
	assert(ioctl(fargs->uffd, UFFDIO_API, &uffdio_api) != -1);
	
	pthread_t uffd_thread;
	assert(pthread_create(&uffd_thread, NULL, handler, (void*)fargs) == 0);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(rank + 8, &cpuset);

	int result = pthread_setaffinity_np(uffd_thread, sizeof(cpu_set_t), &cpuset);
	if (result != 0) {
		perror("Error setting thread affinity");
	}
	
	pthread_t compression_thread;
	assert(pthread_create(&compression_thread, NULL, starts_async_compression, NULL) == 0);
	
	cpu_set_t cpuset_compression;
	CPU_ZERO(&cpuset_compression);
	CPU_SET(rank + 24, &cpuset_compression);
	
	assert(pthread_setaffinity_np(compression_thread, sizeof(cpu_set_t), &cpuset_compression) == 0);

	return ret;
}
