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


#include <openssl/sha.h>

recv_manager_t* manager = NULL;
int recv_count = 0;
int wait_count = 0;

reg_addr_list *reg_list = NULL;

int reg_first = 0;

unsigned char hash[20000];
unsigned char hashprev[20000];

int wrapper_MPI_Isend( void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int index = find_and_create((char*)buf, type_size);
	if(find_and_create((char*)buf, type_size) == 16 && rank == 0){
		
		if(reg_first == 0){
			index = find_and_create((char*)buf, type_size);

			//uffd_register((char*)buf, type_size, rank);
			pair[index].comp_size = compress_lz4_buffer(pair[index].isend_addr, 
					pair[index].isend_size,
					pair[index].comp_addr,
					pair[index].comp_size);
			pair[index].created = 1;


			int page_size = sysconf(_SC_PAGE_SIZE);
			char *region = (char*)((unsigned long)buf & ~(page_size - 1)) + 4096;
			size_t region_size = (type_size + page_size - 1) / page_size * page_size - 4096;
			//char *region = (char*)((unsigned long)data & ~(page_size - 1)) + 4096;
			//size_t region_size = (4096*10 + page_size - 1) / page_size * page_size - 4096;
			printf("register region %p size %ld\n", region, region_size);

			// Step 1: Create a userfaultfd object
			int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
			assert(uffd != -1);

			// Step 2: Register the memory with userfaultfd
			struct uffdio_api uffdio_api;
			uffdio_api.api = UFFD_API;
			uffdio_api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
			assert(ioctl(uffd, UFFDIO_API, &uffdio_api) != -1);

			// Step 3: set up address and flags
			struct uffdio_register uffdio_register;
			uffdio_register.range.start = (unsigned long)region;
			uffdio_register.range.len = region_size;
			uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP; 
			assert(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) != -1);

			struct uffdio_writeprotect uffdio_wp;
			//uffdio_wp.range.start = fault_address & ~(page_size - 1);
			uffdio_wp.range.start = (unsigned long)region;
			uffdio_wp.range.len = region_size;
			uffdio_wp.mode = 0;

			if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
				perror("UFFDIO_WRITEPROTECT");
				exit(EXIT_FAILURE);
			}
			
			uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
			if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1) {
				perror("UFFDIO_WRITEPROTECT");
				exit(EXIT_FAILURE);
			}


			// Step 4: Spawn a thread to handle page faults
			pthread_t uffd_thread;

			struct fault_handler_args *args = (struct fault_handler_args*)malloc(sizeof(struct fault_handler_args));
			args->uffd = uffd;
			args->length = region_size;
			args->address = (void*)region;
			args->rank = rank;

			add_fault_args(uffd, region_size, (void*)region, rank);

			assert(pthread_create(&uffd_thread, NULL, handler, (void*)args) == 0);

		}

		reg_first++;
	
#if 0
		for(int j = 0; j < rand()%100000; j++){
			for(int i = 0; i < 2000; i++){
				((int*)data)[i] += 1;
			}
		}

		int cmpret = memcmp(hashprev, buf, type_size);
		if(cmpret != 0)
			printf("not equal\n");
#endif

		return MPI_Isend( buf, count, type, dest, tag, comm, request );
	}

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
	return MPI_Waitall(count, array_of_requests, array_of_statuses);
}

int wrapper_MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
	int ret = MPI_Init_thread( argc, argv, required, provided );
	reg_list = init_register_list();
	init_fault_list();

	return ret;
}
