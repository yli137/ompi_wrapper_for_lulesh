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

unsigned char hash[SHA256_DIGEST_LENGTH];
unsigned char hashprev[SHA256_DIGEST_LENGTH];

int wrapper_MPI_Isend( const void *buf, int count, MPI_Datatype type, int dest,
		int tag, MPI_Comm comm, MPI_Request *request )
{
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	int type_size;
	MPI_Type_size( type, &type_size );
	type_size *= count;

	int index = find_and_create((char*)buf, type_size);

	if(index == 8){
	
		SHA256_CTX sha256;
		SHA256_Init(&sha256);
		SHA256_Update(&sha256, buf, type_size);
		SHA256_Final(hash, &sha256);

		for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
			if(memcmp(hash, hashprev, SHA256_DIGEST_LENGTH) == 0){
				printf("rank %d there is a difference\n", rank);
				break;
			}

		memcpy(hashprev, hash, SHA256_DIGEST_LENGTH);

		if(reg_first == 0){
			index = find_and_create((char*)buf, type_size);

			uffd_register(pair[index].isend_addr, (size_t)(pair[index].isend_size), rank);
			pair[index].comp_size = compress_lz4_buffer(pair[index].isend_addr, 
					pair[index].isend_size,
					pair[index].comp_addr,
					pair[index].comp_size);
			pair[index].created = 1;
		}

		reg_first++;

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

	return ret;
}
